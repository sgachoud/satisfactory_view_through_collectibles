# Findings from the real FactoryGame headers

Verified against `F:\dev\satisfactory_mods\SatisfactoryModLoader` — SML **3.12.0**, engine
**5.6.1-CSS**, game CL **502094**. `.cpp` bodies in that checkout are stubs; only header
signatures are authoritative.

## Outline system (`FGOutlineComponent.h`, `FGBlueprintFunctionLibrary.h`)

- `static UFGOutlineComponent* UFGOutlineComponent::Get(const UWorld* world)` — local
  player's outline component.
- `AFGCharacterPlayer::GetOutline()` → `UFGOutlineComponent*` (FORCEINLINE) — fallback.
- `void ShowOutline(AActor* actorToOutline, const EOutlineColor color, bool
  createDefaultProxies = true, bool onlyHighlightProxies = false)`
- `void ShowOutlineForStaticMeshComponent(AActor*, UStaticMeshComponent*, const EOutlineColor)`
- `void HideOutline(AActor* actor)`
- `EOutlineColor GetOutlineStateColorForActor(const AActor* actor)` — can be used instead of
  tracking our own current-colour map.

### ⚠ `EOutlineColor` is NOT a colour picker

```
OC_NONE, OC_INPUTOUTPUT, OC_HOLOGRAMLINE, OC_SOFTCLEARANCE, OC_USABLE, OC_HOLOGRAM,
OC_INVALIDHOLOGRAM, OC_RED, OC_DISMANTLE, OC_SOFTCLEARANCEOVERLAP
```

These are semantic states, each bound to a specific look in the game's outline material.
Roughly 3–4 are visually distinct and sensible for "spot a collectible":
`OC_RED` (red), `OC_USABLE` (warm/yellow), `OC_HOLOGRAM` (teal/green), `OC_DISMANTLE`
(orange-red). There is **no arbitrary `FLinearColor` outline path** anywhere in the headers.

The outlines *do* render through terrain/buildings (same custom-depth pass the resource
scanner uses), so the "see through" requirement is fine — only free colour choice is not.

`r.CustomDepth=3` in `Config/DefaultEngine.ini` → the custom depth **+ stencil** pass is on
globally, so a custom post-process outline material (for true per-type RGB) is viable
without any base-game project change.

## Collectible registry (`FGScannableSubsystem.h`, `FGWorldScannableData.h`)

`AFGScannableSubsystem::Get(worldContext)` — the system behind the Object Scanner.

- `const TArray<FWorldScannableData>& GetAvailableItemPickups() const`
- `const TArray<FWorldScannableData>& GetAvailableDropPods() const`
- `bool DoesPickupExist(const FGuid& PickupGuid) const` — false once collected/destroyed
- `bool HasDropPodBeenLooted(const FGuid& dropPodGuid) const`

`FWorldScannableData = { TSoftObjectPtr<AActor> Actor; FGuid ActorGuid;
TSubclassOf<AActor> ActorClass; FVector ActorLocation; }`

This is a **cooked-in list of every level-placed collectible + its class + world position**,
with collected-state filtering — far better than a `TActorIterator` sweep. Use it for
positions/distance-cull, then outline the subset whose `Actor.Get()` is currently streamed
in. Does **not** include runtime-spawned player-dropped items.

**Server-authoritative.** `AFGScannableSubsystem : AFGSubsystem`, and `mAvailableItemPickups`
/ `mAvailableDropPods` are `Transient` (not `Replicated`) — populated by
`AFGWorldScannableDataGenerator` where the cooked data loads. On a **remote client** both
arrays come back empty, so a client must fall back to `TActorIterator<AFGItemPickup>` +
`TActorIterator<AFGDropPod>` (both are `AFGStaticReplicatedActor`, so nearby ones replicate
in normally). The subsystem does this whenever the registry yields zero hits.

The vanilla **Object Scanner** works around this with `Server_SetScannableDescriptor` +
a single `ReplicatedUsing` `FScannableActorDetails mClosestObject` on the equipment — the
server sends back only the closest match for the selected descriptor, no full list. This
mod's optional `UVTCServerFeedSubsystem` does the equivalent for all categories: on the
server it reads the registry and pushes a distance-limited `{pos, category}` array to each
player's `UVTCCollectibleFeedComponent` (a runtime `UActorComponent` on the PlayerController,
`SetIsReplicated(true)` → owner-only). No RPC in either direction; state replication only.

## Pickups (`FGItemPickup.h`, `FGItemPickup_Spawnable.h`)

- `AFGItemPickup` (abstract) : `AFGStaticReplicatedActor` — base of slugs, spheres, sloops,
  flora, and (via `_Spawnable`) dropped items.
  - `TSubclassOf<UFGItemDescriptor> GetPickupItemClass() const` — **use this to categorise**
    (flora & hard-drive have no dedicated pickup BP, only a descriptor).
  - `bool IsPickedUp() const`, `const FGuid& GetItemPickupGuid() const`,
    `FInventoryStack GetPickupItems() const`.
- `AFGItemPickup_Spawnable : AFGItemPickup` (NotPlaceable) — single player-dropped item;
  runtime-spawned, not in scannable data → needs an iterator or spawn hook.
- Multi-item drops (death / dismantle) are `AFGCrate : AFGInteractActor` — a **separate
  hierarchy**, not an `AFGItemPickup`. Decide whether "dropped items" covers crates.

## Drop pods (`FGDropPod.h`)

- `AFGDropPod : AFGCrashSiteBaseActor` (not a pickup) — own iterator / own scannable list.
- `FORCEINLINE bool HasBeenOpened() const`, `bool HasBeenLooted() const` — skip looted pods.

## Verified asset paths

| Category | Pickup actor BP | Item descriptor |
|---|---|---|
| Power slug blue | `/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal.BP_Crystal_C` | `Desc_Crystal_C` |
| Power slug yellow | `.../Crystal/BP_Crystal_mk2.BP_Crystal_mk2_C` | `Desc_Crystal_mk2_C` |
| Power slug purple | `.../Crystal/BP_Crystal_mk3.BP_Crystal_mk3_C` | `Desc_Crystal_mk3_C` |
| Mercer Sphere | `/Game/FactoryGame/Prototype/WAT/BP_WAT1.BP_WAT1_C` | `Desc_WAT1_C` |
| Somersloop | `/Game/FactoryGame/Prototype/WAT/BP_WAT2.BP_WAT2_C` | `Desc_WAT2_C` |
| Beryl Nut | *(no dedicated BP)* | `/Game/FactoryGame/Resource/Environment/Berry/Desc_Berry.Desc_Berry_C` |
| Paleberry | *(no dedicated BP)* | `/Game/FactoryGame/Resource/Environment/Nut/Desc_Nut.Desc_Nut_C` |
| Bacon Agaric | *(no dedicated BP)* | `/Game/FactoryGame/Resource/Environment/DesertShroom/Desc_Shroom.Desc_Shroom_C` |
| Hard drive pod | `AFGDropPod` subclass(es) | `/Game/FactoryGame/Resource/Environment/CrashSites/Desc_HardDrive.Desc_HardDrive_C` |

(Berry/Nut/Shroom folder names don't match their in-game names — `Desc_Berry` = Beryl Nut,
`Desc_Nut` = Paleberry, `Desc_Shroom` = Bacon Agaric. Confirm in-game before shipping.)
