# Step-by-step: from zero to a working build

This is the exact sequence for **this mod**. Where it says "follow the docs page", the
official guide at <https://docs.ficsit.app/satisfactory-modding/latest> is the source of
truth and changes with each game version — don't skip those pages, they have
version-specific numbers (engine 5.6.1-CSS, VS 2022 17.14, Wwise 2023.1.14.8770, etc.).

Mod reference used throughout: **`ViewThroughCollectibles`** (no spaces, PascalCase).

---

## Phase A — Install the toolchain (one time, ~2–4 h, 30+ GB)

Follow **Getting Started → Installing Dependencies → Required Software** end to end:

1. **Satisfactory** installed (Steam or Epic) and launched once.
2. **Visual Studio 2022** (v17.14) with: `.NET Desktop Development`, `Desktop development
   with C++`, `Game development with C++`, plus individual components `MSVC v143 …
   (v14.38-17.8)`, `.NET 8.0 Runtime`, `.NET Framework 4.8.1 SDK`. Import the docs'
   `.vsconfig` to get these exactly.
3. **Custom Unreal Engine 5.6.1-CSS**:
   - Link GitHub ↔ Epic Games (join the EpicGames org). Verify:
     <https://github.com/EpicGames/UnrealEngine/> loads (not 404).
   - Link GitHub to the modding org via the tool the docs link.
   - Download + install the custom engine ("Unreal Engine - CSS").
4. **Wwise**: install Audiokinetic Launcher, sign in, install Wwise **2023.1.14.8770**.
5. **Satisfactory Mod Manager** (SMM) installed — you already use this.
6. Optional: SirDigby's **SMEH** tool automates most of A.

---

## Phase B — Get the starter project (the Unreal project you build mods in)

The `SatisfactoryModLoader` repo **is** the Unreal project. Pick a short path on a big drive.

```bash
git clone https://github.com/satisfactorymodding/SatisfactoryModLoader D:/SML
```

1. Check out the branch/release matching your installed game version (the repo README /
   releases page says which). Confirm `FactoryGame.uproject` has
   `"EngineAssociation": "5.6.1-CSS"`.
2. **Wwise integration**: Wwise Launcher → Unreal Engine tab → *Open other* →
   `D:/SML/FactoryGame.uproject` → *Integrate Wwise in Project* → Integration Version
   **All** → pick `2023.1.14.8770` → clear the Wwise Project path (dropdown → New project)
   → **Integrate**.
3. **Generate sound banks**: open `D:/SML/SatisfactoryModLoader_WwiseProject/…wproj` →
   Project Explorer → SoundBanks → right-click top folder → *Generate SoundBank(s) for all
   platforms* → close Wwise.
4. **Generate VS files**: right-click `FactoryGame.uproject` → *Generate Visual Studio
   project files* (or the `Build.bat … -projectfiles` command from the docs).
5. **Build the editor**: open `FactoryGame.sln` in VS → set config **Development Editor** /
   **Win64** / project **FactoryGame** → Build. This takes a long time the first run.
6. Launch: open `FactoryGame.uproject` (double-click, or F5 from VS). Dismiss the
   first-run pop-ups per the docs. You now have the modding editor.

---

## Phase C — Link this repo into the SML checkout

`mod/ViewThroughCollectibles/` in this repo **is the complete Unreal plugin** — source,
`.uplugin`, `Build.cs`, `Config/`, and the built `Content/*.uasset` assets. You don't
scaffold anything with Alpakit; you just make the SML checkout point at this folder with a
directory junction, so there's one copy and edits are always live in git.

Paths here use this machine: SML at `F:\dev\satisfactory_mods\SatisfactoryModLoader`, this
repo at `F:\dev\satisfactory_mods\view_throught_collectibles`.

1. Close the Unreal editor (it locks the plugin's DLLs).
2. Create the junction. SML's game-feature mods live under `Mods\GameFeatures\<ModRef>\`.
   From an ordinary (non-admin) PowerShell:
   ```powershell
   New-Item -ItemType Junction `
     -Path   "F:\dev\satisfactory_mods\SatisfactoryModLoader\Mods\GameFeatures\ViewThroughCollectibles" `
     -Target "F:\dev\satisfactory_mods\view_throught_collectibles\mod\ViewThroughCollectibles"
   ```
   (`cmd /c mklink /J "<Path>" "<Target>"` does the same thing.) The `Path` must not
   already exist — delete or move any previous copy first.
3. `Binaries/`, `Intermediate/` and `Saved/` get generated *inside* `mod/ViewThroughCollectibles/`
   through the junction. They're in `.gitignore` — never commit them.
4. Regenerate VS project files (right-click `FactoryGame.uproject` → *Generate Visual
   Studio project files*) and build the **FactoryEditor / Development / Win64** target with
   the editor closed.
5. Open `FactoryGame.uproject`. With *Show Plugin Content* on you should see
   **ViewThroughCollectibles Content** with the 5 assets already present.

The C++ compiles and the content assets load at this point — Phases D and E below are
**reference only** (how those assets were built / how to regenerate them after a schema
change), not steps you need to repeat for a fresh checkout.

---

## Phase D — Build the config UI (SML ModConfiguration asset) — *reference*

> Already done — `Content/ViewThroughCollectibles_Config.uasset` and
> `RootGameInstance_ViewThroughCollectibles.uasset` are in the repo. Follow this only if
> you change the config schema and need to rebuild them.

The in-game menu comes from a Blueprint asset, and SML **generates the C++ struct from
it** — so the asset is the source of truth, and this repo's `VTCConfig.h` struct is a
placeholder that the generated header will replace.

1. In *ViewThroughCollectibles Content*, right-click → Blueprint Class → (All Classes)
   `ModConfiguration` → name it `ViewThroughCollectibles_Config`.
2. Open it. Set **Mod Reference** = `ViewThroughCollectibles`, **Display Name** =
   "View Through Collectibles", fill Description.
3. Set **Root Section** = `BP Config Property Section`. In its *Section Properties* add the
   fields exactly as listed in [`CONTENT-ASSETS.md`](CONTENT-ASSETS.md) §3 (mirrors
   `FVTCConfigStruct`): `MaxDistanceMeters`/`RefreshIntervalSeconds`/`OutlineThicknessPixels`/
   `OccludedFillOpacity`/`RemoteMarkerMaxDistanceMeters` (Float), `MaxSimultaneousOutlines`
   (Int), `ShowOnMap` (Bool), and one **Section** per collectible type with `Enabled` (Bool)
   + `Color` (**String** — hex `RRGGBB`; SML 3.12 has no colour property type).
   Also build `MPC_VTCColors` and `M_VTCOutline` per §1–§2 of that doc.
4. Create a **Game Instance Module** BP (right-click → Blueprint Class →
   `GameInstanceModule`) named `RootGameInstance_ViewThroughCollectibles`. Open it, tick
   **Is Root Module**, and add `ViewThroughCollectibles_Config` to its **Mod
   Configurations** list.
5. Right-click `ViewThroughCollectibles_Config` → **Generate C++ Configuration Header** →
   save into `Source/ViewThroughCollectibles/Public/` (let it replace/inform `VTCConfig.h`
   — keep the generated `GetActiveConfig` / struct field names in sync with
   `VTCOutlineSubsystem.cpp`; adjust the subsystem's field references if the generator
   names them differently).
6. Right-click again → **Regenerate Configuration Structs**. Re-run this after every
   schema change.
7. Preview without launching: Content Browser → *SMLEditor Content/Menu Preview* →
   right-click `SML_MenuPreviewWidget` → *Run Editor Utility Widget* → *Set Selected Mod
   Config*.

---

## Phase E — Resolve the remaining verification items

The C++ module and the content assets build clean. What's left can only be confirmed with
the editor running or in-game, and is marked `TODO(verify …)` in the source / docs:

| Where | What to confirm |
|---|---|
| `M_VTCOutline` Custom node | SceneTexture ids are confirmed against `MaterialTemplate.ush` (SceneDepth=1, CustomDepth=13, PostProcessInput0=14, CustomStencil=25). Left: that the material compiles and reads stencil 201–210 correctly on a live scene. |
| `LoadCategoryTables()` flora entries | Folder names are mismatched — `Desc_Berry` = Beryl Nut? Pick one up in-game and check the item, or fix via `Game.ini` `[ViewThroughCollectibles.Categories]`. |
| `BP_Crystal` mesh | Whether world power slugs use regular `UMeshComponent`s. If they're abstract/instanced, Custom Depth won't apply — outline the `AFGItemPickup` proxy mesh instead. |
| `VTCConfig.cpp` `GetActiveConfig` | Replace body with the editor-generated accessor if its shape differs from `FillConfigurationStruct(FConfigId, FDynamicStructInfo)`. |

Build **Development Editor** after each round of changes.

---

## Phase F — Package and test

1. **Alpakit** panel (main toolbar button, or *Window → Alpakit*). Open its settings:
   - **Windows** tab → **Copy to Game Path** = your Satisfactory install
     (`F:\Programme et jeux\Steam\steamapps\common\Satisfactory` on this machine),
     **Launch Game Type** = `Steam` (or `None` to launch it yourself).
   - **Disable the server targets.** This mod is client-only (`"RequiredOnRemote": false`
     in the `.uplugin`, subsystem returns false on dedicated servers), so it has no server
     side. Leaving Windows Server / Linux Server enabled makes packaging fail with
     `Platform Linux is not a valid platform to build` unless you've also installed the
     UE 5.6 Linux cross-compile toolchain — which you don't need. Package **Windows** only.
2. Tick `ViewThroughCollectibles` → **Alpakit Selected**. It builds the Shipping DLL/PDB,
   cooks `Content/`, and copies the mod into `<Satisfactory>/FactoryGame/Mods/ViewThroughCollectibles/`.
3. Launch the game (or let Alpakit launch it). SMM in developer mode will list the mod.
4. Run the checks in the repo `README.md` → "Verifying":
   - config menu shows all types + distance / thickness / fill;
   - singleplayer: coloured see-through outlines within distance, drop past it, live toggle,
     live colour change, dropped item outlines;
   - multiplayer: host + client — per-client independent config, no replication warnings,
     save unchanged; joining a server that does **not** have the mod succeeds (client-only)
     and outlines still work for you;
   - perf: frame time OK in a slug-dense area at default `MaxSimultaneousOutlines`.

---

## Phase G — Iterate

Because the checkout is a junction to this repo, editor asset saves and C++ edits land
directly in `mod/ViewThroughCollectibles/` — `git status` in this repo shows them.

- Blueprint/asset changes: compile+save in editor (writes into the repo), re-Alpakit.
- C++ class-structure changes: close editor → build Development Editor → reopen.
- C++ body-only changes: Unreal Live Coding (Ctrl+Alt+F11) usually suffices.
- Config schema changes: re-run *Regenerate Configuration Structs*.
- Commit `.uasset` changes as binary; keep `Binaries/ Intermediate/ Saved/` out of git.

## Troubleshooting seen on this machine

- **`LINK : fatal error LNK1181: cannot open input file 'delayimp.lib'`** on every DLL link —
  the MSVC v14.38 build tools installed were the **ARM64/ARM** variant, not **x64/x86**
  (`VC\Tools\MSVC\14.38.33130\lib\` had only `arm\`, no `x64\`). Fix: VS Installer → Modify →
  Individual components → check **"MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.38-17.8)"**.
  Already-compiled `.obj` files are cached, so the re-run resumes.
- Alpakit's game-feature templates create the plugin under
  `SatisfactoryModLoader\Mods\GameFeatures\<ModRef>\`, not `Mods\<ModRef>\`. Content mount
  point is still `/<ModRef>/`.

## Phase H — Release (later)

Alpakit Edit Mod → fill metadata + icon (≥128²) → **Alpakit Release** → upload the result
to <https://ficsit.app> per the "Uploading to SMR" docs page.
