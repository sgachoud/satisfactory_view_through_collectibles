# View Through Collectibles

A Satisfactory (SML) mod that draws a configurable coloured outline on world collectibles
and dropped items, visible **through** grass, buildings and terrain, within a distance you
choose.

Covers: hard-drive drop pods, all three power-slug tiers, Mercer Spheres, Somersloops,
collectible flora (Beryl Nut, Paleberry, Bacon Agaric), and items dropped on the ground.
Each type is independently toggleable and has its own RGB outline colour.

## How it works

- A client-side `UWorldSubsystem` asks the game's `AFGScannableSubsystem` for every
  level-placed collectible (class + world position + collected/looted state), plus sweeps
  for runtime player-dropped items.
- For the nearest N within the configured distance, it writes a per-category **Custom Depth
  stencil value** (201–210) onto the collectible's mesh components.
- A packaged **post-process material** (`M_VTCOutline`) reads those stencil values and draws
  a coloured edge (and a faint fill where the collectible is hidden behind geometry).
  Colours, thickness and fill opacity come from a Material Parameter Collection
  (`MPC_VTCColors`) that the subsystem updates live from the mod config.

Custom Depth + stencil is already enabled in the base game (`r.CustomDepth=3`), so no
project-level change is required.

## Multiplayer

Safe — the effect is entirely client-side and cosmetic:

- The subsystem never runs on a dedicated server (`ShouldCreateSubsystem` returns false).
- The only actor it spawns is a transient, client-local `APostProcessVolume` — never
  replicated, never saved (`RF_Transient`, no `IFGSaveInterface`).
- It only reads already-replicated collectible positions and sets rendering flags.
- Config is per client. Two players in one session can run completely different settings.

## Configuration

In-game via the mod's settings menu (Mod Manager → this mod → Config, or the pause-menu mod
options):

| Setting | Meaning |
|---|---|
| **Max Distance (m)** | Collectibles farther than this from you are not outlined. |
| **Refresh Interval (s)** | How often the in-range set is recomputed. |
| **Max Simultaneous Outlines** | Safety cap; nearest collectibles win. |
| **Outline Thickness (px)** | Edge width in screen pixels. |
| **Occluded Fill Opacity** | Tint strength over the parts hidden behind geometry. |
| **Per type**: Enabled + Color | Show/hide each collectible family and pick its RGB colour. |

## Layout

```
mod/ViewThroughCollectibles/        SML C++ plugin
  Source/.../Public/VTCTypes.h        collectible categories (= stencil offsets) + per-type settings
  Source/.../Public/VTCConfig.h       SML config struct + live-config accessor
  Source/.../Private/VTCOutlineSubsystem.cpp   the whole effect
docs/FINDINGS.md                     verified FactoryGame API + asset paths
docs/CONTENT-ASSETS.md               exact build spec for M_VTCOutline / MPC_VTCColors / config assets
docs/SETUP.md                        toolchain + integration walkthrough
```

## Building it

Requires Satisfactory's account-gated C++ toolchain. Full walkthrough in
[docs/SETUP.md](docs/SETUP.md); asset specs in [docs/CONTENT-ASSETS.md](docs/CONTENT-ASSETS.md).
Short version:

1. Set up the toolchain and a `SatisfactoryModLoader` checkout (docs.ficsit.app).
2. Alpakit → Create Mod → "C++ and Blueprint" template, name `ViewThroughCollectibles`;
   copy/merge this repo's `Source/` into it.
3. Build the four editor assets from `docs/CONTENT-ASSETS.md` (material, MPC,
   ModConfiguration, GameInstanceModule).
4. Build the Development Editor target; resolve any API drift (a few `TODO(verify …)`
   comments remain — SceneTexture ids, flora descriptor names).
5. Alpakit-deploy and test.

## Verifying

No local build without the toolchain. Once it builds:

1. **Config** — the settings widget shows every type + distance / thickness / fill.
2. **Singleplayer** — near a crash site: pods / slugs / sloops within the distance get a
   coloured see-through outline; walking past the distance drops them within one refresh;
   toggling a type off or changing its colour updates live. Drop an item → it outlines (if
   Dropped Items is enabled).
3. **Multiplayer** — host + a second client (or a dedicated server): the dedicated server
   log shows the subsystem was not created; each client follows its own config and
   position; no replication warnings; quitting leaves no residual actors, save unchanged.
4. **Perf** — in a slug-dense area, confirm frame time is fine at the default
   `MaxSimultaneousOutlines`; lower it if not.

## Open verification items

Not design problems — things only confirmable with the toolchain, marked `TODO(verify …)`
in the source / docs:

- SceneTexture ids in the `M_VTCOutline` Custom node (SceneDepth / PostProcessInput0 /
  CustomDepth / CustomStencil).
- Flora descriptor → in-game-name mapping (`Desc_Berry` folder ≠ Beryl Nut necessarily).
- Whether world power slugs use regular `UMeshComponent`s (Custom Depth won't apply to
  abstract-instanced meshes — check `BP_Crystal`).
- SML `UConfigManager::FillConfigurationStruct` call shape vs the generated accessor.
