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

## Phase C — Create the mod plugin (via Alpakit) and drop this repo's code in

The `.uplugin` and source files in `mod/ViewThroughCollectibles/` of this repo are the
**intended end state**. The clean way to get there is to let Alpakit scaffold the plugin,
then copy this repo's `Source/` over the generated one.

1. In the editor: **Window → Alpakit Dev** (dock it somewhere).
2. **Create Mod** → template **"C++ and Blueprint"** → Mod Name: `ViewThroughCollectibles`
   → leave *Show Content Directory* checked → **Create Mod**.
   This creates `D:/SML/Mods/ViewThroughCollectibles/` with a `.uplugin`, a `Source/`
   folder, a module class, and a `Content/` folder.
3. Close the editor.
4. Replace the generated source with this repo's:
   - Copy `mod/ViewThroughCollectibles/Source/ViewThroughCollectibles/Public/*` and
     `.../Private/*` from **this repo** into
     `D:/SML/Mods/ViewThroughCollectibles/Source/ViewThroughCollectibles/`.
   - Merge `Build.cs`: make sure `PublicDependencyModuleNames` contains at least
     `Core, CoreUObject, Engine, DeveloperSettings, SML, FactoryGame` (this repo's
     `ViewThroughCollectibles.Build.cs` is the reference).
   - If Alpakit's template made its own `…Module.cpp`, keep **one** module
     implementation. This repo ships a minimal
     `ViewThroughCollectiblesModule.cpp` using `IMPLEMENT_GAME_MODULE`; if you keep
     Alpakit's `IMPLEMENT_MODULE` version instead, delete this repo's file. Don't have both.
   - Merge the `.uplugin`: it must have the `Modules` array (one `Runtime` module named
     `ViewThroughCollectibles`, `LoadingPhase: Default`), `"Plugins": [{ "Name": "SML",
     "Enabled": true }]`, and `"CanContainContent": true`. This repo's `.uplugin` is the
     reference.
   > Tip: instead of copying, make `D:/SML/Mods/ViewThroughCollectibles` a directory
   > symlink to this repo's `mod/ViewThroughCollectibles` so edits stay in git:
   > `mklink /D D:\SML\Mods\ViewThroughCollectibles F:\dev\satisfactory_mods\view_throught_collectibles\mod\ViewThroughCollectibles`
   > (then still reconcile `.uplugin` / module as above).
5. Regenerate VS project files (right-click `FactoryGame.uproject`).
6. Build **Development Editor / Win64 / FactoryGame** again (editor closed).
7. Open the editor. Under *Plugins* content (enable *Show Plugin Content*) you should see
   **ViewThroughCollectibles Content**.

At this point `RefreshOutlines()` etc. compile. Expect to fix a few
`TODO(needs FactoryGame source access)` spots — see Phase E.

---

## Phase D — Build the config UI (SML ModConfiguration asset)

The in-game menu comes from a Blueprint asset, and SML **generates the C++ struct from
it** — so the asset is the source of truth, and this repo's `VTCConfig.h` struct is a
placeholder that the generated header will replace.

1. In *ViewThroughCollectibles Content*, right-click → Blueprint Class → (All Classes)
   `ModConfiguration` → name it `ViewThroughCollectibles_Config`.
2. Open it. Set **Mod Reference** = `ViewThroughCollectibles`, **Display Name** =
   "View Through Collectibles", fill Description.
3. Set **Root Section** = `BP Config Property Section`. In its *Section Properties* add,
   matching this repo's `FVTCConfigStruct`:
   - `MaxDistanceMeters` → Float (default 120, min 10, max 1000)
   - `RefreshIntervalSeconds` → Float (default 0.5)
   - `MaxSimultaneousOutlines` → Int (default 128)
   - one **Section** per collectible type (`HardDrivePods`, `PowerSlugsBlue`, …,
     `DroppedItems`), each containing:
     - `bEnabled` → Bool
     - `Color` → **Class** property pointing at the outline colour enum *or* (simpler) an
       Int/enum selector 0–5 that you map to `EOutlineColor` in code. See Phase E note.
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

## Phase E — Resolve the source-dependent TODOs

With the FactoryGame headers now available (`D:/SML/Source/FactoryGame/…`), search them
and fix each `TODO(needs FactoryGame source access)`:

| Where | What to confirm |
|---|---|
| `VTCOutlineSubsystem.cpp` `GetLocalOutlineComponent()` | The real way to get the local player's `UFGOutlineComponent` — grep `FGOutlineComponent.h` for a static `GetOutlineComponent` / check `AFGCharacterPlayer` for a `GetOutline()` accessor. |
| same file, `ShowOutline` / `HideOutline` calls | Exact signatures + whether there's a batch (`TArray<AActor*>`) overload. |
| `VTCTypes.h` `EOutlineColor` | Real enumerator names. If there's no plain enum (it's driven by custom primitive data / a curve), switch `FVTCTypeSettings::Color` to an `int32`/local `UENUM` and translate in `ApplyOutline`. |
| `#include` block in `VTCOutlineSubsystem.cpp` | Real header paths for `AFGItemPickup`, `AFGItemPickup_Spawnable`, `AFGDropPod`, `AFGCharacterPlayer`. |
| `VTCConfig.cpp` `GetActiveConfig` | Replace body with the generated one, or match `UConfigManager::FillConfigurationStruct(FConfigId, …)` exactly. |
| `LoadClassCategoryTable()` paths | Verify every BP path (see `collectible-classes.md`) — in the editor, browse `FactoryGame Content` or use FModel on the packaged game. |

Build **Development Editor** after each round of changes.

---

## Phase F — Package and test

1. **Alpakit Dev** window → configure Dev Packaging Settings once (point it at your
   Satisfactory install, enable "start game after packing" if you like).
2. Tick `ViewThroughCollectibles` → **Alpakit Selected**. It builds Shipping + the DLL/PDB
   and copies the mod into `<Satisfactory>/FactoryGame/Mods/`.
3. Launch the game (or let Alpakit launch it). SMM in developer mode will list the mod.
4. Run the checks in the repo `README.md` → "Verifying":
   - config menu shows all types + distance slider;
   - singleplayer: outlines through terrain within distance, hide past it, live toggle,
     live colour change, dropped item outlines;
   - multiplayer: host + client (and/or a dedicated server) — subsystem absent on the
     dedicated server log, per-client independent, no replication warnings, save unchanged;
   - hammer on a drop pod (and with Long Reach if installed) — no `ShowOutline` crash.

---

## Phase G — Iterate

- Blueprint/asset changes: compile+save in editor, re-Alpakit.
- C++ class-structure changes: close editor → build Development Editor → reopen.
- C++ body-only changes: Unreal Live Coding (Ctrl+Alt+F11) usually suffices.
- Config schema changes: re-run *Regenerate Configuration Structs*.

## Phase H — Release (later)

Alpakit Edit Mod → fill metadata + icon (≥128²) → **Alpakit Release** → upload the result
to <https://ficsit.app> per the "Uploading to SMR" docs page.
