# View Through Collectibles

A Satisfactory/SML mod that highlights nearby hard-drive pods, power slugs, Mercer
Spheres, Somersloops, collectible flora and dropped items through terrain and buildings.
Each category has its own toggle and colour. Optional dots show the same selection on
the map. A dot appears on the compass only while you highlight its map marker.

## Supported modes

| Session | Behaviour |
| --- | --- |
| Solo | The authoritative scannable registry supplies collectible positions and collected state, including unloaded objects. |
| Listen-server host | Same discovery and presentation as solo, using the host's own config. |
| Remote client, server without this mod | Loaded collectibles and dropped items are highlighted. Distant registry markers are unavailable because collected state cannot be verified. |
| Remote client, server with this mod | The server supplies authoritative distant collectibles using that client's distance, count, enabled categories and refresh interval. |
| Server with clients without this mod | SML's installed-mod list is checked before creating the owner-only feed component. Unsupported clients receive no mod component. |
| Dedicated server | Runs the optional feed service only; creates no outline volumes or marker actors. |

Install matching mod versions on both ends for the full multiplayer feature set.
`RequiredOnRemote: false` makes server installation optional; it does not prohibit a
server build.

## Selection and presentation

Discovery uses persistent collectible GUIDs. Runtime dropped items without a usable
persistent identity receive a local ID for their lifetime. Loaded actors replace distant
records with the same ID; nearby but distinct collectibles are never merged.
On a modded server, a loaded level-placed actor cannot reintroduce an ID absent from
the server selection. Observed collected IDs also suppress stale feed entries after
streaming, so both map dots and world highlights use the same collected-state filter.

Enabled categories and distance are filtered **before** choosing the nearest N.
**Max Simultaneous Outlines is one shared budget** across loaded outlines and distant
markers. Zero distance or zero count disables the selection.

A post-process material reads category stencils 201–210. Loaded meshes get an outline;
unloaded objects, or actors without an available mesh, get a small sphere at the
collectible's position. Marker and map-dot creation is spread across refreshes, at most
20 of each per refresh. Map dots retain their identity and update colour/location live.

The mod preserves mesh stencil settings and restores them when it releases a mesh.
Meshes already using Custom Depth are left to their existing owner; a marker is used
when no mesh can be claimed. If another system takes over a claimed mesh, the mod
relinquishes it without restoring stale state. World teardown and loss of the local
pawn clear all local indicators.

The shader uses the game's existing Custom Depth/stencil pass. While presentation is
active, the mod requests `r.CustomDepthTemporalAAJitter=0`; overlapping worlds share
the override, and a later user console override is preserved.

## Multiplayer requests

The server waits for the client's first settings report. It never substitutes its own
config, even when the client requests zero distance or count. Invalid/non-finite requests
are ignored. Valid requests have no hidden distance/count ceiling or minimum of 400.
Large selections therefore intentionally cost more processing and bandwidth.

Queries are scheduled on a 0.1-second server timer and refreshed at each client's requested
interval (minimum 0.1 seconds), plus scheduling/network delay. The registry is gathered
once for all clients due on that tick. Replies carry a request revision so a response to
old settings is not reused after a config change. Feed entries have a stable ID order
to avoid replication changes caused only by exchanging distance ranks. Responses are
sent in acknowledged pages of 128 entries; only complete snapshots are exposed. A slow
connection finishes its current response before another scan, so large selections may
refresh more slowly than the configured interval.

Colours, thickness, fill and map visibility are applied locally and do not need server
configuration. The feed covers level-placed collectibles; runtime dropped items are
discovered from loaded actors on the client.

## Configuration

Default distance: 120 m. Default refresh: 0.4 s. Default shared limit: 200.
Flora and dropped items are disabled by default. Colours accept hex RGB/RGBA, with an
optional `#`; malformed strings fall back to white. See [usage](docs/USAGE.md).

## Development

- [Build and package](docs/SETUP.md)
- [Architecture and API notes](docs/FINDINGS.md)
- [Content asset specification](docs/CONTENT-ASSETS.md)
- [Category overrides](docs/collectible-classes.md)
- [Automated tests and multiplayer validation](docs/TESTING.md)

The plugin lives in `mod/ViewThroughCollectibles`. Discovery and nearest-N selection
are separate from local presentation and from the optional server feed. Content assets
are committed; generated binaries and intermediate files are ignored.

## License

MIT — see [LICENSE](LICENSE).
