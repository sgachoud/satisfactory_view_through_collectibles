# Building and packaging

The local SML project is at `F:\dev\satisfactory_mods\SatisfactoryModLoader`.
The installed engine is `C:\Program Files\Unreal Engine - CSS` (5.6.1-CSS).

For a fresh machine, follow the official guide at https://docs.ficsit.app/satisfactory-modding/latest
for your game/SML version. This checkout uses SML 3.12.0, game CL 502094,
VS 2022/MSVC v14.38 x64/x86 and Wwise 2023.1.14.8770.

## Integrate and build

Junction the complete `mod/ViewThroughCollectibles` folder into
`<SML>/Mods/GameFeatures/ViewThroughCollectibles`. Source, config and content are
already present. Regenerate project files when needed and close the editor before
compiling structural C++ changes.

```powershell
& 'C:/Program Files/Unreal Engine - CSS/Engine/Build/BatchFiles/Build.bat' FactoryEditor Win64 Development '-Project=F:/dev/satisfactory_mods/SatisfactoryModLoader/FactoryGame.uproject' -WaitMutex -NoHotReloadFromIDE
```

The build writes generated files into the SML checkout and engine/user caches as well
as the plugin's ignored Binaries and Intermediate folders.

Run the automation group and game matrix in [TESTING.md](TESTING.md). Editor builds
alone cannot verify packaged-game behaviour: FactoryGame implementation files in the
modding checkout are stubs.

## Content/config changes

The committed assets include the post-process material, colour collection, config,
root game-instance module, game-feature asset and map-dot texture. Their specification
is in [CONTENT-ASSETS.md](CONTENT-ASSETS.md).

FVTCConfigStruct mirrors the SML config asset's field names. Preserve its sanitised
accessors and feed-preference conversion when regenerating a configuration header.
Register the config in RootGameInstance_ViewThroughCollectibles and regenerate
configuration structs after schema changes.

## Package

Use Alpakit to cook/package the plugin:

- Windows client: needed for solo, listen hosts and remote players.
- Windows Server / Linux Server: build the platforms used by dedicated servers.
  Linux requires the matching Unreal cross-compilation toolchain.
- Disable only the targets you do not intend to distribute.

RequiredOnRemote: false means a peer may omit this mod. The plugin does contain an
optional server service. Full distant markers require compatible client/server versions;
version 0.2 introduces the identity/preferences/page protocol.

Point Alpakit's copy target at the intended game/server installation. Run the test
matrix before releasing. Deployment and publishing are separate from source changes.

## Publish to SMM through ficsit.app

The descriptor's integer `Version` must equal the major number of `SemVersion`:
for `0.2.0`, use `Version: 0`. Keep `VersionName` equal to `SemVersion`. Check this
in every platform's packaged descriptor before uploading.

Use **Alpakit Release** with Windows, Windows Server and Linux Server selected.
It includes both Steam and Epic Windows binaries and produces the combined upload
at `<SML>/Saved/ArchivedPlugins/ViewThroughCollectibles/ViewThroughCollectibles.zip`.
The separate platform ZIPs are not the release upload.

Equivalent command for this checkout (packages without deploying or launching):

```powershell
& 'C:/Program Files/Unreal Engine - CSS/Engine/Build/BatchFiles/RunUAT.bat' '-ScriptsForProject=F:/dev/satisfactory_mods/SatisfactoryModLoader/FactoryGame.uproject' PackagePlugin '-project=F:/dev/satisfactory_mods/SatisfactoryModLoader/FactoryGame.uproject' -clientconfig=Shipping -serverconfig=Shipping -utf8output -DLCName=ViewThroughCollectibles -build -platform=Win64 -server -serverplatform=Win64+Linux -nocompileeditor -installed -merge -UbtArgs=-NoUBA
```

The command assumes the editor binaries needed for cooking already exist. Rebuild
the editor separately when changing classes referenced by content assets.

Sign in to [ficsit.app](https://ficsit.app), choose Mods > New Mod and use the fields
in [the listing draft](release/LISTING.md). The immutable mod reference is
`ViewThroughCollectibles`. Keep the listing visible, then choose New Version,
upload the combined ZIP and paste [the changelog](release/CHANGELOG.md).
Only claim compatibility for game branches actually tested. C++ releases must pass
the repository's automated approval before becoming downloadable.

See the [official upload guide](https://docs.ficsit.app/satisfactory-modding/latest/UploadToSMR.html).

## Known toolchain issue

For LNK1181 concerning delayimp.lib, check that the MSVC v14.38 x64/x86 build tools
are installed; the ARM64/ARM package alone is not enough.
