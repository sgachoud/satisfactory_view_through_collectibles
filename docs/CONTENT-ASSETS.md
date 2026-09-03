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

Right-click → **Material**. Name it `M_VTCOutline`. In **Details**:

- **Material Domain**: `Post Process`
- **Blendable Location**: `After Tonemapping` (predictable colour; try `Before Tonemapping`
  if you want it to bloom)
- **Blendable Priority**: leave 0
- **Output Alpha**: unchecked (we write Emissive only)

Nodes to place:

1. **Collection Parameter** node → set **Collection** = `MPC_VTCColors`. From it you can drag
   each parameter out (`OutlineThicknessPixels`, `OccludedFillOpacity`, `Color201`…`Color210`).
2. **Scene Texture** node → **Id = `PostProcessInput0`**. Its presence is what makes
   `SceneTextureLookup` compile inside the Custom node; wire its **Color** (RGB) into the
   Custom node's `SceneColor` input so it isn't dead-stripped.
3. **Custom** node — **Output Type = `CT_Float3`**, output → **Emissive Color**.

Custom node **inputs** (Name → wire):

| Input pin | Wire from |
|---|---|
| `UV` | `ScreenPosition` node → **ViewportUV** output (or `TexCoord[0]`) |
| `SceneColor` | Scene Texture (PostProcessInput0) → **Color** (take RGB) |
| `Thickness` | Collection Param `OutlineThicknessPixels` |
| `FillOpacity` | Collection Param `OccludedFillOpacity` |
| `C201` … `C210` | the 10 Collection Param `Color2xx` (each a float4) |

Custom node **Code** (verified against this engine's `MaterialTemplate.ush`):

```hlsl
// Screen-space texel size (viewport).
float2 texel = View.ViewSizeAndInvSize.zw;
float  thick = max(Thickness, 1.0);

// CustomStencil (id 25) returns the raw stencil integer in .r (e.g. 201.0), not normalised.
// Clamp taps so screen-edge samples don't wrap.
#define STEN(uv) (SceneTextureLookup(clamp((uv), texel, 1.0 - texel), 25, false).r)
#define OURS(s)  ((s) > 200.5 && (s) < 210.5)

float sC = STEN(UV);
bool  centerOurs = OURS(sC);

// 8-tap ring at the thickness radius. Count mismatches for a *fractional* edge, and
// adopt a neighbour's category so the outline can extend one ring outside the silhouette.
float2 dirs[8] =
{
    float2(-1,-1), float2(0,-1), float2(1,-1),
    float2(-1, 0),               float2(1, 0),
    float2(-1, 1), float2(0, 1), float2(1, 1)
};

float ourStencil = centerOurs ? sC : 0.0;
int   mismatch   = 0;
[unroll] for (int i = 0; i < 8; ++i)
{
    float s = STEN(UV + dirs[i] * texel * thick);
    if (OURS(s) && ourStencil < 200.5) ourStencil = s;
    if (s != sC) mismatch++;
}
if (ourStencil < 200.5) return SceneColor;

// Pick this category's colour
int iv = (int)(ourStencil + 0.5);
float4 col = C210;
if      (iv == 201) col = C201; else if (iv == 202) col = C202;
else if (iv == 203) col = C203; else if (iv == 204) col = C204;
else if (iv == 205) col = C205; else if (iv == 206) col = C206;
else if (iv == 207) col = C207; else if (iv == 208) col = C208;
else if (iv == 209) col = C209;
if (col.a <= 0.001) return SceneColor;  // category disabled (subsystem sets alpha 0)

// Fractional silhouette coverage -> feathers the edge over ~1px, much less crawl than a
// binary test. 3+ mismatched neighbours = a full-strength edge pixel.
float edgeAmount = saturate(mismatch / 3.0);

// Occluded fill: our pixel that sits behind scene geometry.
// CustomDepth (13) and SceneDepth (1) are both linear world depth in cm. The 20 cm bias
// keeps the fill boundary from shimmering against the still-jittered scene depth.
float customDepth = SceneTextureLookup(UV, 13, false).r;
float sceneDepth  = SceneTextureLookup(UV, 1,  false).r;
bool  occluded    = centerOurs && (sceneDepth + 20.0 < customDepth);

float a = max(edgeAmount * col.a, occluded ? saturate(FillOpacity) * col.a : 0.0);
return lerp(SceneColor, col.rgb, a);
```

> The edge is still screen-space and un-accumulated, so fast camera motion will show some
> crawl — eliminating that entirely needs a temporal history buffer, out of scope here.
> The subsystem also forces `r.CustomDepthTemporalAAJitter 0` while active (restored on
> unload), which removes the Custom Depth pass's own TSR/TAA jitter.

SceneTexture ids (confirmed in `Engine/Shaders/Private/MaterialTemplate.ush` for 5.6.1-CSS):
`1` SceneDepth, `13` CustomDepth, `14` PostProcessInput0, `25` CustomStencil.

> Custom Depth **+ stencil** is already enabled in the base game (`r.CustomDepth=3` in
> `Config/DefaultEngine.ini`), so no project change is needed.

---

## 3. `ViewThroughCollectibles_Config` — ModConfiguration

Follow `docs/SETUP.md` Phase D. The schema must mirror `FVTCConfigStruct` field-for-field
(the subsystem reads by name):

- `MaxDistanceMeters` float, `RefreshIntervalSeconds` float, `MaxSimultaneousOutlines` int
- `RemoteMarkerMaxDistanceMeters` float (multiplayer marker feed distance; 0 disables. The
  C++ default is 250, so the mod works without this field — add it only to expose it in the menu.)
- `OutlineThicknessPixels` float, `OccludedFillOpacity` float, `ShowOnMap` bool
- Sections `HardDrivePods`, `PowerSlugsBlue`, `PowerSlugsYellow`, `PowerSlugsPurple`,
  `MercerSpheres`, `Somersloops`, `BerylNut`, `Paleberry`, `BaconAgaric`, `DroppedItems`,
  each with `Enabled` bool + `Color` **String** property.

SML 3.12 has no colour property type, so `Color` is a hex string — `"RRGGBB"` or
`"RRGGBBAA"`, leading `#` optional. `FVTCTypeSettings::GetLinearColor()` parses it via
`FColor::FromHex`. This asset is already built and committed; regenerate the C++ header
from it only after a schema change and reconcile with `VTCConfig.h`.

## 4. `RootGameInstance_ViewThroughCollectibles` — GameInstanceModule

Blueprint Class → `GameInstanceModule`. Tick **Is Root Module**. Add
`ViewThroughCollectibles_Config` to **Mod Configurations**. Without this the config is never
registered and `GetActiveConfig()` returns defaults forever.
