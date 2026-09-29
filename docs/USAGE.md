# Usage

Enable **View Through Collectibles** in Satisfactory Mod Manager. Open its in-game mod
configuration to adjust the settings.

| Setting | Effect |
| --- | --- |
| Max Distance (m) | Maximum distance from your pawn. Default 120; zero disables indicators. |
| Refresh Interval (s) | Discovery refresh interval, including server queries when supported. Default 0.4; minimum 0.1. |
| Max Simultaneous Outlines | Shared nearest-N limit for real outlines and distant sphere markers. Default 200; zero disables indicators. |
| Outline Thickness (px) | Outline width; minimum 1. |
| Occluded Fill Opacity | Tint behind geometry, from 0 to 1. |
| Show on Map | Adds map dots for the selected collectibles. Highlight a dot to show it on the compass; unhighlight it to hide it again. Off by default. |
| Per-type Enabled / Color | Category toggle and hex colour, for example `FF7300` or `FF730080`. |

Settings apply on refresh, without restarting. Existing map dots update when colours
change. Creating large numbers of new markers/dots is spread across refreshes.

Collectible map dots and their highlighted state are local to your client. They are
not saved or shared as player-created map markers. Closing and reopening a dot's
popup preserves its highlight while the collectible remains in your selection.
Use the mod configuration to change collectible appearance and visibility; the
native marker editor's Apply action does not save changes to these automatic dots.

## Multiplayer

Your configuration controls your own selection. A modded server follows each client's
distance, count, categories and refresh interval, independently of the host's settings.
Use matching mod versions on client and server.

Without the mod on the server, loaded collectibles and dropped items still work.
Distant sphere markers are unavailable in this mode. Install the mod on the server to
add authoritative distant positions and collected-state filtering.

Solo and listen-server hosts have authoritative discovery locally. Players without
the mod receive no feed component from a modded server.

## Limitations

- Large ranges/counts increase CPU, rendering and network work.
- Server scheduling, replication and the configured refresh interval introduce delay.
- Fast camera motion can cause shimmer in the screen-space outline.
- Actors without a usable mesh use a sphere marker.
- Multi-item death/dismantle crates are not included in the dropped-item category.
- This mod does not add persistent save data or modify collectible gameplay.
