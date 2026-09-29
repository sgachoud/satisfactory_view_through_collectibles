# Architecture and API notes

The local integration targets SML 3.12.0, FactoryGame CL 502094 and Unreal 5.6.1-CSS.
FactoryGame implementation files in the modding checkout are stubs; header signatures
and real game tests must be distinguished.

## Discovery

`VTCDiscovery.cpp` reads `AFGScannableSubsystem` on authority only. Its registry stores
`FWorldScannableData`: actor soft reference, GUID, actor class and position.
`DoesPickupExist` and `HasDropPodBeenLooted` provide authoritative collected state.
An empty authoritative result stays empty; it never falls back to raw cooked data.

The position arrays are transient. Transient does **not** imply empty on remote clients:
cooked generator data can populate positions there, while save-backed destroyed/looted
sets remain unavailable. Consequently remote clients never use that registry as proof
that a collectible still exists.

Loaded `AFGItemPickup` and `AFGDropPod` actors resolve current positions and remove
collected entries by GUID. With a server feed, they may only enrich level-placed IDs
present in that authoritative selection, including while waiting for the first response.
Local collection observations persist across streaming and older feed snapshots; a
respawnable pickup is eligible again after it is loaded with replenished items. The iterator already includes
`AFGItemPickup_Spawnable`, so dropped items are not swept twice.
`GetItemPickupGuid` and `GetDropPodGuid` provide stable level-placed identities.
Runtime identities are local and retained only for the actor's lifetime.

`VTCSelection.h` applies category/distance filters, deterministic distance ordering
(with GUID tie-breaks), and one shared count limit.

## Optional server feed

`USMLRemoteCallObject::IsClientModInstalled` and the client's version gate component
creation. The listen host needs no feed, and clients without the mod receive no custom
replicated class.

`UVTCCollectibleFeedComponent` accepts validated client preferences and a request
revision. Work happens on the subsystem timer, not inside the RPC. The server waits
for a report instead of using host settings. Responses contain GUID, quantised position
and category. Only one page of at most 128 entries is in flight per owner; the client
acknowledges each page and publishes only a complete response. This bounds network
message size without truncating the client's configured selection.

The server filters categories before its count limit. The client merges that nearest-N
level-placed set with loaded actors and dropped items and applies the same selection
policy. Zero settings are intentional disable requests.

## Presentation

`UVTCOutlineSubsystem` owns local meshes, transient markers, a post-process volume and
map representations. GUIDs link their lifetimes. Marker roots are created before setting
world position. Markers render in Custom Depth only, without main/depth-pass rendering,
shadows, collision or replication.

Mesh Custom Depth ownership is tracked per component. Original stencil values and write
masks are restored only if the component still has the state written by this mod.
Pre-existing Custom Depth is not taken over.

`UVTCMapRepresentation` updates local location, colour and text before asking
`AFGActorRepresentationManager` to refresh the existing representation. Labels use
localisable text literals rather than editor-only enum display metadata.

## Verification still requiring the game

- World-partition loading and collection while moving between areas.
- Full host/client and dedicated-server replication, including clients without the mod.
- Flora blueprint/descriptor mappings and their actual rendered mesh types.
- Shader output under TSR/TAA and different screen percentages.
- Interaction with the game's scanner highlights and other Custom Depth users.

See [TESTING.md](TESTING.md) for the regression and manual test matrix.