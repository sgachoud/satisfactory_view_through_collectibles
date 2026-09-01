# View Through Collectibles

A Satisfactory (SML) mod that draws a configurable outline on world collectibles and
dropped items, visible **through** grass, buildings and terrain, within a distance you
choose.

Covers: hard-drive drop pods, all three power-slug tiers, Mercer Spheres, Somersloops,
collectible flora (Beryl Nut, Paleberry, Bacon Agaric), and items dropped on the ground.
Each type is independently toggleable and has its own outline colour.

## Multiplayer

Safe. The effect is entirely client-side and cosmetic:

- The subsystem never runs on a dedicated server (`ShouldCreateSubsystem` returns false
  there).
- It never spawns or replicates an actor, and never writes to the save.
- It only reads already-replicated collectible positions and drives the local player's
  existing `UFGOutlineComponent` — the same outline system the resource scanner uses.
- Config is per client. Two players in the same session can run completely different
  settings without affecting each other.

## Configuration

In-game via the mod's settings menu (Mod Manager → this mod → Config, or the pause-menu
mod options):

| Setting | Meaning |
|---|---|
| **Max Distance (m)** | Collectibles farther than this from you are not outlined. |
| **Refresh Interval (s)** | How often the in-range set is recomputed. |
| **Max Simultaneous Outlines** | Safety cap; nearest collectibles win. |
| **Per type**: Enabled + Color | Show/hide each collectible family and pick its outline colour. |

### Colour palette caveat

The game's outline component only supports a **fixed set of named colours**
(`EOutlineColor`: red / green / blue / orange / …), not a free RGB picker. The colour
dropdown reflects that. If a future FactoryGame source drop exposes a custom-RGB outline
entry point, `FVTCTypeSettings::Color` can be widened to `FLinearColor` and only
`UVTCOutlineSubsystem`'s show call needs to change.

## Layout

```
mod/ViewThroughCollectibles/        SML C++ plugin
  Source/.../Public/VTCTypes.h        collectible categories + per-type settings struct
  Source/.../Public/VTCConfig.h       SML config struct + live-config accessor
  Source/.../Public/VTCOutlineSubsystem.h
  Source/.../Private/VTCOutlineSubsystem.cpp   the whole effect: sweep, distance-cull, diff, outline
docs/collectible-classes.md          class-path table to verify against a real install
```

## Setting up the toolchain (you do this part)

Requires Satisfactory's C++ modding toolchain, which is account-gated:

1. Follow [docs.ficsit.app](https://docs.ficsit.app) → Development → Getting Started, top to
   bottom (Epic account link for FactoryGame source, Visual Studio 2022 with the Game
   Development with C++ workload, and a `SatisfactoryModLoader` checkout — that repo *is*
   the Unreal project you build mods inside).
2. Symlink (or copy) `mod/ViewThroughCollectibles/` into
   `<SatisfactoryModLoader checkout>/Mods/ViewThroughCollectibles`.
3. Open the project in the Unreal Editor. Generate the SML **ModConfiguration** asset from
   `FVTCConfigStruct` using SML's Configuration Tool (Modding → Configuration), so the
   in-game settings widget exists. Keep the generated asset under this plugin's `Content/`.
4. Build. Resolve any FactoryGame API drift flagged by `TODO(needs FactoryGame source
   access)` comments — mainly:
   - the outline component accessor (`UFGOutlineComponent::GetOutlineComponent` / the
     `AFGCharacterPlayer` fallback) and `ShowOutline` / `HideOutline` signatures;
   - the `EOutlineColor` enumerator names;
   - the `AFGItemPickup` / `AFGItemPickup_Spawnable` / `AFGDropPod` header paths;
   - the SML `UConfigManager::FillConfigStruct` call in `VTCConfig.cpp`.
5. Verify the collectible class paths in `docs/collectible-classes.md` against the packaged
   game (FModel or the editor) and fix `LoadClassCategoryTable()` or add `Game.ini`
   overrides.
6. Use **Alpakit** to build and deploy locally. Satisfactory Mod Manager's developer mode
   then lists it like any other local mod.

## Verifying

No local build is possible without the toolchain above. Once it builds:

1. **Config** — the settings widget shows every type + the distance slider.
2. **Singleplayer** — near a crash site: pods / slugs / sloops within the set distance are
   outlined through terrain and buildings; walking past the distance hides them; toggling a
   type off hides it within one refresh tick; changing a colour updates it. Drop an item →
   it outlines (if Dropped Items is enabled).
3. **Multiplayer** — host + a second client (or a dedicated server): the dedicated server
   log shows the subsystem was not created; each client's outlines follow its own config
   and position; no replication warnings; quitting leaves no residual actors and the save
   is unchanged.
4. **Drop-pod crash guard** — repeatedly look at / Long-Reach a drop pod; confirm no
   `ShowOutline … invalid index` ensure/crash.

## Known TODOs

All marked `TODO(needs FactoryGame source access)` in the source. None are blocking design
issues — they are API names and asset paths that can only be confirmed with the toolchain.
