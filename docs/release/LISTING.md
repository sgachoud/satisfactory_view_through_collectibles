# New ficsit.app listing

## Name

View Through Collectibles

## Mod reference

`ViewThroughCollectibles`

## Short description

Find collectibles through terrain and buildings with configurable outlines, local map dots and compass highlighting. Solo and multiplayer supported.

## Full description

Spot hard-drive drop pods, power slugs, Mercer Spheres, Somersloops, collectible flora and dropped items through terrain, buildings and vegetation.

### Features

- Choose which collectible types to show, with a separate colour for each type.
- Adjust viewing distance, outline thickness, occluded fill and the maximum number of indicators.
- Enable optional map dots. Highlight a collectible's map dot to show it on the compass; unhighlight it to hide the compass icon.
- Collectible map dots and their highlighted state stay local to your client. They are not shared player-created markers and are not saved.
- Collected objects are removed from the selection as their collected state updates.

### Getting started

Install **View Through Collectibles** with Satisfactory Mod Manager, then open its in-game mod configuration. The default range is 120 metres, with up to 200 indicators and a 0.4-second refresh interval. Flora, dropped items and map dots are disabled by default.

Turn on **Show on Map** to enable collectible map dots. Use the mod configuration to change their appearance; the normal marker editor's Apply action does not save changes to these automatic dots.

### Solo and multiplayer

In solo and when hosting, the mod can discover distant collectibles and their collected state from the authoritative world registry.

When joining a server without this mod, detection is limited to loaded collectibles and dropped items. Install the same mod version on the server to enable authoritative distant collectible discovery. The server follows each client's distance, enabled categories, count limit and refresh interval. Server installation is optional.

The release includes Windows clients (Steam and Epic), Windows dedicated servers and Linux dedicated servers.

### Limitations

- Increasing distance or the indicator limit increases processing, rendering and, in multiplayer, network work.
- Updates take time according to the refresh interval, server scheduling and network connection.
- Unloaded collectibles, and objects without a usable mesh, use sphere indicators instead of mesh outlines.
- Fast camera movement can cause outline shimmer.
- Multi-item death and dismantle crates are not included in the dropped-item category.

This is a **0.2.0 beta** release. Please report problems with your game version, mod version and whether you were playing solo, hosting or joining a server with or without the mod.

[Report a bug](https://github.com/sgachoud/satisfactory_view_through_collectibles/issues)

## Source URL

https://github.com/sgachoud/satisfactory_view_through_collectibles

## Network Activity Transparency

The mod makes no external network requests. Its optional multiplayer component exchanges collectible information and client settings through the game's multiplayer connection.

## AI Usage Transparency

Generative AI assisted with source-code development, debugging, documentation, the mod listing and release notes. The mod does not provide access to generative AI during gameplay.

## Publication notes (not part of the description)

- The mod reference must be exactly `ViewThroughCollectibles`.
- Leave Hidden disabled so the mod can appear in SMM and search.
- The existing Resources/Icon128.png is the generic default mod icon; the listing icon can be left blank.
- Paste the disclosure text into the site's dedicated transparency fields.
- Upload the combined `ViewThroughCollectibles.zip`, not an individual platform ZIP.
- Use CHANGELOG.md for the version changelog.
- Mark a game branch as Works only after testing it. Building and packaging alone do not verify in-game compatibility.
- Verify any additional AI-assisted asset creation from earlier development is included in the AI disclosure before submitting.

Publishing instructions: https://docs.ficsit.app/satisfactory-modding/latest/UploadToSMR.html

Content policy: https://ficsit.app/content-policy
