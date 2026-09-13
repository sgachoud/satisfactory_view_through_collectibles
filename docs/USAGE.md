# View Through Collectibles — usage

A see-through, per-type coloured outline on world collectibles and dropped items, within a
distance you choose. Client-side and cosmetic — safe to add to any game, singleplayer or
multiplayer.

## Install

Get it from [ficsit.app](https://ficsit.app) with the Satisfactory Mod Manager (or your
server's mod tools) and enable **View Through Collectibles**. It has one dependency, SML,
which the manager installs for you. Nothing to configure to get started — the defaults work.

## What it covers

Hard-drive drop pods · Power Slugs (blue / yellow / purple, coloured separately) · Mercer
Spheres · Somersloops · collectible flora (Beryl Nut, Paleberry, Bacon Agaric) · items
dropped on the ground.

Each type is toggled independently and has its own outline colour.

## Configuration

Open the mod's settings (Mod Manager → this mod → Config, or the pause-menu mod options).

| Setting | Meaning |
|---|---|
| **Max Distance (m)** | Collectibles farther than this are not shown. Default 120. |
| **Refresh Interval (s)** | How often the visible set is recomputed. Default 0.4. |
| **Max Simultaneous Outlines** | Safety cap; the nearest collectibles win. Default 200. |
| **Outline Thickness (px)** | Edge width in screen pixels. |
| **Occluded Fill Opacity** | Tint strength over the part hidden behind terrain / buildings. |
| **Show on Map** | Also drop a coloured dot on the map and compass for each active outline. Off by default. |
| **Per type: Enabled + Color** | Show/hide each family and pick its RGB colour (hex, e.g. `FF7300`). |

All settings apply live — no restart. In multiplayer each player has their own settings.

## Multiplayer

- **Client-only.** You can add it to your game and join servers that don't have it; other
  players are unaffected.
- Collectibles you can see get an outline; ones a little too far to have loaded in get a
  small see-through marker sphere instead, so nearby collectibles show before they render.
- If the **server also has the mod**, that marker fill-in works out to the full distance on
  a server too, and it filters out collectibles that have already been picked up. Without it
  on the server, distant collectibles are read from the map's own scan data: they still show,
  but ones collected earlier by other players may keep showing until you get close enough for
  the game to tell you they're gone.

## Known limitations

- The outline edge is screen-space and not temporally smoothed, so fast camera motion shows
  some shimmer on the edge. The mod already disables the Custom Depth pass's TAA jitter to
  cut most of it.
- On a server without the mod, "already collected" state for distant, not-yet-loaded
  collectibles isn't available to the client, so a few collected ones can linger on the map
  until you approach them. Collectibles you've loaded in are always filtered correctly.
- World Power Slugs that render as instanced meshes may not take the outline; pick one up
  and it will show as a dropped item.

## Compatibility

Satisfactory 1.1+ (game CL ≥ 502094), SML `^3.12.0`. Doesn't touch your save, doesn't
change gameplay.

## Links

- Source & issues: <https://github.com/sgachoud/satisfactory_view_through_collectibles>
