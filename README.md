# View Through Collectibles

A Satisfactory (SML) mod that draws a configurable coloured outline on world collectibles
and dropped items, visible **through** grass, buildings and terrain, within a distance you
choose.

Covers: hard-drive drop pods, all three power-slug tiers, Mercer Spheres, Somersloops,
collectible flora (Beryl Nut, Paleberry, Bacon Agaric), and items dropped on the ground.
Each type is independently toggleable and has its own RGB outline colour.

## How it works

- A client-side `UWorldSubsystem` asks the game's `AFGScannableSubsystem` for every
  level-placed collectible (class + world position + collected/looted state). That registry
  is server-authoritative and unreplicated, so on a remote client it comes back empty and
  the subsystem falls back to sweeping streamed-in `AFGItemPickup` / `AFGDropPod` actors.
  Runtime player-dropped items are always found by a direct `AFGItemPickup_Spawnable` sweep.
- For the nearest N within the configured distance, it writes a per-category **Custom Depth
  stencil value** (201–210) onto the collectible's mesh components.
- Collectibles between the outline distance and **Remote Marker Max Distance** get a small
  see-through marker sphere — same stencil, same colours — so you can see what's out there
  before it renders. Positions come from the scannable registry in singleplayer / as the
  listen-server host; on a remote client they're replicated from the server **if the server
  also has the mod** (a server-side subsystem feeds each client an owner-only list).
  Without the mod on the server the markers simply don't appear and the outlines still work
  for whatever is loaded.
- A packaged **post-process material** (`M_VTCOutline`) reads those stencil values and draws
  a coloured edge (and a faint fill where the collectible is hidden behind geometry).
  Colours, thickness and fill opacity come from a Material Parameter Collection
  (`MPC_VTCColors`) that the subsystem updates live from the mod config.
- Optionally (**Show on Map**), each active outline/marker also gets a category-coloured
  dot on the map and compass via `AFGActorRepresentationManager` (client-local, no actor).

Custom Depth + stencil is already enabled in the base game (`r.CustomDepth=3`), so no
project-level change is required. While the subsystem is active it also forces
`r.CustomDepthTemporalAAJitter 0` (restored on unload) so the outline doesn't inherit the
TSR/TAA sub-pixel jitter and shimmer.

## Multiplayer

Safe — the effect is client-side and cosmetic:

- The mod is **client-only** (`"RequiredOnRemote": false` in the `.uplugin`): a server
  doesn't need it, and you can join servers that don't have it. Players without the mod are
  unaffected.
- The client outline subsystem never runs on a dedicated server (`ShouldCreateSubsystem`
  returns false). The only actors it spawns are transient, client-local: one
  `APostProcessVolume` and the marker spheres — never replicated, never saved
  (`RF_Transient`, no `IFGSaveInterface`).
- **Optional server half**: if the server has the mod, `UVTCServerFeedSubsystem` runs there
  and replicates a distance-limited list of `{position, category}` to each player's own
  `UVTCCollectibleFeedComponent` (owner-only). It reads the existing scannable registry,
  spawns nothing, never touches the save. Purely additive — absent, clients just fall back.
- Config is per client. Two players in one session can run completely different settings.

## Configuration

In-game via the mod's settings menu (Mod Manager → this mod → Config, or the pause-menu mod
options):

| Setting | Meaning |
|---|---|
| **Max Distance (m)** | Collectibles farther than this from you are not outlined. |
| **Remote Marker Max Distance (m)** | Collectibles out to this range get a see-through marker sphere before they render. 0 disables. On a remote client this needs the mod on the server too. |
| **Refresh Interval (s)** | How often the in-range set is recomputed. |
| **Max Simultaneous Outlines** | Safety cap; nearest collectibles win. |
| **Outline Thickness (px)** | Edge width in screen pixels. |
| **Occluded Fill Opacity** | Tint strength over the parts hidden behind geometry. |
| **Show on Map** | Also put a category-coloured dot on the map and compass for everything currently outlined or markered. |
| **Per type**: Enabled + Color | Show/hide each collectible family and pick its RGB colour. |

## Layout

```
mod/ViewThroughCollectibles/        the complete SML plugin (junctioned into the SML checkout)
  ViewThroughCollectibles.uplugin    "RequiredOnRemote": false — client-only, no server build
  Source/.../Public/VTCTypes.h        collectible categories (= stencil offsets) + per-type settings
  Source/.../Public/VTCConfig.h       SML config struct + live-config accessor
  Source/.../Public/VTCCategoryTables.h        descriptor/class -> category, shared by both subsystems
  Source/.../Private/VTCOutlineSubsystem.cpp   client: outlines + distant-collectible markers
  Source/.../Private/VTCServerFeedSubsystem.cpp   server (optional): feeds nearby collectibles to clients
  Source/.../Private/VTCCollectibleFeedComponent.cpp   replicated server->owning-client list
  Content/Materials/M_VTCOutline       post-process outline material
  Content/Materials/MPC_VTCColors      per-category colour / thickness parameters
  Content/ViewThroughCollectibles_Config          SML ModConfiguration (the in-game menu)
  Content/RootGameInstance_ViewThroughCollectibles GameInstanceModule (registers the config)
docs/FINDINGS.md                     verified FactoryGame API + asset paths
docs/CONTENT-ASSETS.md               exact build spec for M_VTCOutline / MPC_VTCColors / config assets
docs/SETUP.md                        toolchain + integration walkthrough
```

`Binaries/`, `Intermediate/`, `Saved/` appear in `mod/ViewThroughCollectibles/` once it's
built through the SML checkout; they're gitignored.

## Building it

Requires Satisfactory's account-gated C++ toolchain. Full walkthrough in
[docs/SETUP.md](docs/SETUP.md); asset specs in [docs/CONTENT-ASSETS.md](docs/CONTENT-ASSETS.md).
Short version:

1. Set up the toolchain and a `SatisfactoryModLoader` checkout (docs.ficsit.app) — custom
   engine `5.6.1-CSS`, VS 2022 with **MSVC v14.38 x64/x86**, Wwise 2023.1.14.8770.
2. Junction this repo's `mod/ViewThroughCollectibles/` into the checkout at
   `Mods/GameFeatures/ViewThroughCollectibles/` (it's the whole plugin — source *and* the
   four `Content/` assets; nothing to scaffold). See [docs/SETUP.md](docs/SETUP.md) Phase C.
3. Regenerate VS project files, build the **FactoryEditor / Development / Win64** target.
4. In Alpakit, disable the server targets (client-only mod — see below), point "Copy to
   Game Path" at your Satisfactory install, **Alpakit Selected**, test.

A few `TODO(verify …)` items remain — flora descriptor names, power-slug mesh type, the
outline material's behaviour on a live scene — confirmable only in the editor / in-game.

## Verifying

No local build without the toolchain. Once it builds:

1. **Config** — the settings widget shows every type + distance / thickness / fill.
2. **Singleplayer** — near a crash site: pods / slugs / sloops within the distance get a
   coloured see-through outline; walking past the distance drops them within one refresh;
   toggling a type off or changing its colour updates live. Drop an item → it outlines (if
   Dropped Items is enabled).
3. **Multiplayer** — host + a second client: each client follows its own config and
   position; no replication warnings; quitting leaves no residual actors, save unchanged.
   Joining a server without the mod succeeds (client-only) and outlines still work for you.
4. **Perf** — in a slug-dense area, confirm frame time is fine at the default
   `MaxSimultaneousOutlines`; lower it if not.

## Open verification items

Not design problems — things only confirmable in the editor / in-game, marked
`TODO(verify …)` in the source / docs:

- Flora descriptor → in-game-name mapping (`Desc_Berry` folder ≠ Beryl Nut necessarily).
- Whether world power slugs use regular `UMeshComponent`s (Custom Depth won't apply to
  abstract-instanced meshes — check `BP_Crystal`).
- SML `UConfigManager::FillConfigurationStruct` call shape vs the generated accessor.
- `M_VTCOutline` compiling and reading stencil correctly once applied to a live scene
  (the SceneTexture ids 1/13/14/25 are confirmed against `MaterialTemplate.ush`).

## Credits

Designed and implemented with Claude Sonnet 5 by Anthropic.
