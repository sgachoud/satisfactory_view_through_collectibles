# Content assets to build in the editor

The C++ subsystem expects four assets in the mod's Content folder (mount point
`/ViewThroughCollectibles/`). Build them once in the Unreal editor, compile + save, and
they get packaged by Alpakit.

```
/ViewThroughCollectibles/
  Materials/MPC_VTCColors            Material Parameter Collection
  Materials/M_VTCOutline             Post Process material (uses MPC_VTCColors)
  Config/ViewThroughCollectibles_Config          ModConfiguration (SML)
  RootGameInstance_ViewThroughCollectibles       GameInstanceModule (registers the config)
```

Paths matter — `VTCOutlineSubsystem.cpp` hard-codes
`/ViewThroughCollectibles/Materials/M_VTCOutline` and `.../MPC_VTCColors`. If you put them
elsewhere, update `OutlineMaterialPath` / `ColorCollectionPath` at the top of that file.

---

## 1. `MPC_VTCColors` — Material Parameter Collection

Right-click → Materials → **Material Parameter Collection**.

**Scalar Parameters**

| Name | Default |
|---|---|
| `OutlineThicknessPixels` | `2.0` |
| `OccludedFillOpacity` | `0.12` |

**Vector Parameters** — one per collectible category, named by its Custom Depth stencil
value (`200 + category index + 1`):

| Name | Category | Suggested default (RGBA) |
|---|---|---|
| `Color201` | Hard Drive Drop Pods | `1.00, 0.45, 0.05, 1` |
| `Color202` | Power Slugs (Blue) | `0.10, 0.55, 1.00, 1` |
| `Color203` | Power Slugs (Yellow) | `1.00, 0.85, 0.10, 1` |
| `Color204` | Power Slugs (Purple) | `0.65, 0.20, 1.00, 1` |
| `Color205` | Mercer Spheres | `0.10, 1.00, 0.55, 1` |
| `Color206` | Somersloops | `1.00, 0.10, 0.35, 1` |
| `Color207` | Beryl Nut | `0.55, 0.85, 0.20, 1` |
| `Color208` | Paleberry | `0.60, 0.80, 0.95, 1` |
| `Color209` | Bacon Agaric | `0.90, 0.55, 0.35, 1` |
| `Color210` | Dropped Items | `0.95, 0.95, 0.95, 1` |

The subsystem overwrites all of these from the mod config every refresh; the defaults only
matter before the first push. **Alpha channel**: the subsystem sets `A = 0` for a disabled
category (the material treats alpha 0 as "draw nothing"), and `A = colour alpha` otherwise.

---

## 2. `M_VTCOutline` — Post Process material

Right-click → **Material**. In **Details**:

- **Material Domain**: `Post Process`
- **Blendable Location**: `After Tonemapping` (predictable colour; try `Before Tonemapping`
  if you want it to bloom)
- **Output Alpha**: unchecked (we write Emissive only)

Add one **Collection Parameter** node pointing at `MPC_VTCColors` (gives you all the params).

Add a **Scene Texture** node with **Id = PostProcessInput0** somewhere in the graph (even
unused) — this makes `SceneTextureLookup` available inside the Custom node below.

Then add a **Custom** node, set **Output Type = CT_Float3**, and wire its output into
**Emissive Color**. Give it these inputs:

| Input pin | Wire from |
|---|---|
| `UV` | `TexCoord[0]` (or `ScreenPosition` → `ViewportUV`) |
| `Thickness` | MPC scalar `OutlineThicknessPixels` |
| `FillOpacity` | MPC scalar `OccludedFillOpacity` |
| `C201`..`C210` | the 10 MPC vector params (RGBA → float4 each) |

Custom node **Code**:

```hlsl
// Screen-space texel size
float2 texel = View.ViewSizeAndInvSize.zw;
float2 o = texel * max(Thickness, 0.5);

// Custom Stencil at center + 4 neighbours
#define STEN(uv) (SceneTextureLookup(uv, 25, false).r * 255.0 + 0.5)
float sC = STEN(UV);
float sL = STEN(UV + float2(-o.x, 0));
float sR = STEN(UV + float2( o.x, 0));
float sU = STEN(UV + float2(0, -o.y));
float sD = STEN(UV + float2(0,  o.y));

// Is a stencil value one of ours? (201..210)
#define OURS(s) ((s) >= 200.5 && (s) <= 210.5)

// The category stencil for this pixel: prefer the center, else the first neighbour that is ours
float v = OURS(sC) ? sC :
          OURS(sL) ? sL :
          OURS(sR) ? sR :
          OURS(sU) ? sU :
          OURS(sD) ? sD : 0.0;
if (v < 200.5) return SceneTextureLookup(UV, 14, false).rgb; // PostProcessInput0, unchanged

// Pick the colour for this category
float4 col;
int iv = (int)round(v);
if      (iv == 201) col = C201; else if (iv == 202) col = C202;
else if (iv == 203) col = C203; else if (iv == 204) col = C204;
else if (iv == 205) col = C205; else if (iv == 206) col = C206;
else if (iv == 207) col = C207; else if (iv == 208) col = C208;
else if (iv == 209) col = C209; else                col = C210;
if (col.a <= 0.001) return SceneTextureLookup(UV, 14, false).rgb; // category disabled

// Edge = center differs from any neighbour (and at least one side is ours)
bool edge = (sC != sL || sC != sR || sC != sU || sC != sD) &&
            (OURS(sC) || OURS(sL) || OURS(sR) || OURS(sU) || OURS(sD));

// Occluded fill: our silhouette pixel that is hidden behind scene geometry
float customDepth = SceneTextureLookup(UV, 24, false).r; // CustomDepth
float sceneDepth  = SceneTextureLookup(UV, 1,  false).r; // SceneDepth
bool occludedFill = OURS(sC) && (sceneDepth + 5.0 < customDepth);

float a = edge ? col.a : (occludedFill ? saturate(FillOpacity) * col.a : 0.0);

float3 scene = SceneTextureLookup(UV, 14, false).rgb; // PostProcessInput0
return lerp(scene, col.rgb, a);
```

SceneTexture ids used: `1` SceneDepth, `14` PostProcessInput0, `24` CustomDepth,
`25` CustomStencil. Confirm against the tooltips on a SceneTexture node in your engine
build if a lookup returns garbage.

> Custom Depth **+ stencil** is already enabled in the base game (`r.CustomDepth=3` in
> `Config/DefaultEngine.ini`), so no project change is needed.

---

## 3. `ViewThroughCollectibles_Config` — ModConfiguration

Follow `docs/SETUP.md` Phase D. The schema must mirror `FVTCConfigStruct` field-for-field
(the subsystem reads by name):

- `MaxDistanceMeters` float, `RefreshIntervalSeconds` float, `MaxSimultaneousOutlines` int
- `OutlineThicknessPixels` float, `OccludedFillOpacity` float
- Sections `HardDrivePods`, `PowerSlugsBlue`, `PowerSlugsYellow`, `PowerSlugsPurple`,
  `MercerSpheres`, `Somersloops`, `BerylNut`, `Paleberry`, `BaconAgaric`, `DroppedItems`,
  each with `bEnabled` bool + `Color` **Color** property.

Generate the C++ header from it and reconcile with `VTCConfig.h`.

## 4. `RootGameInstance_ViewThroughCollectibles` — GameInstanceModule

Blueprint Class → `GameInstanceModule`. Tick **Is Root Module**. Add
`ViewThroughCollectibles_Config` to **Mod Configurations**. Without this the config is never
registered and `GetActiveConfig()` returns defaults forever.
