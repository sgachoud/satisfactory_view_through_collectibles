# Verification

## Automated Unreal tests

`Private/VTCTests.cpp` defines the `ViewThroughCollectibles` automation group:

- **Selection:** category filtering before the limit, shared nearest-N count, stable
  ordering, distinct colocated objects, identity replacement and zero/invalid settings.
- **ClientPreferences:** independent client settings, no server fallback, refresh timing,
  revision acknowledgements, invalid RPC rejection, paged delivery and stale-response rejection.
- **CollectedState:** authoritative empty responses, stale loaded actors and snapshots,
  collection across streaming, regrown plants, runtime drops and solo/fallback filtering.
- **Configuration:** malformed colours, alpha and non-finite saved settings.
- **Presentation:** marker world position and reuse, original mesh state restoration,
  preserving another system's stencil, live map colour, highlight-driven compass visibility and pawn-loss cleanup.

Build the FactoryEditor Development Win64 target, then run this group from the Unreal
Session Frontend Automation window. A headless equivalent is:

```powershell
& '<CSS engine>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<SML>/FactoryGame.uproject' -unattended -nullrhi -nosound '-ExecCmds=Automation RunTests ViewThroughCollectibles' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<report directory>'
```

FactoryGame game-code stubs in the editor cannot prove packaged-game replication or
rendering. Automation supplements the following game tests.

## Required game matrix

| Mode | Verify |
| --- | --- |
| Solo | Loaded and unloaded collectibles, collected pods/pickups disappearing, dropped items, map/compass, save reload. |
| Listen host + modded client | Give host and client different distances, counts and category masks. Each sees their own nearest-N set. |
| Dedicated server + modded client | Same client-driven behaviour; server creates no marker actors or rendering subsystem work. |
| Modded client + server without mod | Joining succeeds; loaded collectibles work; no distant raw-map ghosts or RPC errors. |
| Modded server + client without mod | Joining succeeds; no missing-class/component replication warnings. |
| Two modded clients | Distinct settings remain independent, including one client disabling everything. |

For each supported mode:

1. Set count to one beside both loaded and unloaded candidates: only the nearest selected
   object is represented. Toggle the nearest category off and verify the next eligible one.
2. Change distance/count/categories while moving, including zero values. Old responses must
   not repopulate markers after the change.
3. Enable flora and test dense areas; nearby disabled flora must not exclude enabled pods.
4. Put two collectibles within one metre; both remain distinct when count permits.
5. Change colour while map dots exist; map, compass and outline colours update.
   Unhighlighted collectible dots must stay off the compass. Highlight a map dot,
   close the map, and confirm its compass icon appears. Unhighlight it and confirm
   the icon disappears. Repeat in solo and on modded/unmodded servers; another
   player's highlight must not change your compass. Collect or filter out a
   highlighted collectible and confirm its map dot and compass icon both disappear.
   Repeat highlight/unhighlight on the same dot without changing its colour or
   position. Close and reopen its popup after each change: its toggle must agree
   with the compass. The popup uses a UFGMapMarkerRepresentation and FMapMarker GUID
   lookup, then calls the map manager directly. Both that path and the representation
   API must read and write the same local highlight state. Confirm a normal player-
   created marker still works. Applying edits to a collectible popup must not create
   a saved/shared marker; test this with a second client and after save/reload, also
   when the collectible disappears while its popup is open. State-only assertions
   do not verify this packaged UI path. Clicks handled by the collectible popup
   integration log `LogVTCMapHighlight: ... local highlight on/off` for diagnosis.
6. Collect items, die/respawn, change possession, travel and reconnect. Old markers disappear
   and replacement controllers report their preferences again.
7. Use the scanner or another Custom Depth effect. Existing highlights must survive the
   mod releasing its own meshes.
8. Verify sphere locations far from world origin and under world-partition streaming.
9. Profile larger configured counts and multiple clients; check CPU, network and render
   cost and confirm normal logs are not written every refresh.
10. Package and test Windows client and the server platform actually deployed.

## Current validation status

The marker identity, native popup highlight integration and local-only marker write
safeguards compiled and linked successfully for FactoryGameSteam Win64 Shipping and
FactoryServer Linux Shipping on 2026-09-18. The final build completed in 46 seconds
with this module's shared precompiled header disabled. Diff whitespace checks passed.

Native API inspection confirmed that the marker popup resolves only
UFGMapMarkerRepresentation objects with valid FMapMarker GUIDs and reads/writes
highlight through the map manager. Generic representations tagged RT_MapMarker do
not satisfy that lookup. The deployed DLL matched the previous build before this fix.

The editor automation tests and packaged in-game regression have not been run.
These build checks did not package or deploy the new binaries into the running game.

## Collected-state multiplayer regression

Use a save containing already-looted pods and collected slugs/artifacts. Join a modded
server near those locations and enable both outlines and Show on Map. They must remain
absent even while their actors are loaded. Then collect an available object locally and
with a second player, wait for the configured refresh/network delay, and confirm both
its world indicator and map/compass dot disappear. Move away until the actor unloads,
return, and change category/distance settings: older feed data must not restore it.
Check a harvested plant separately: it may return only when actually replenished.
