#include "VTCDiscovery.h"
#include "VTCCollectionState.h"
#include "VTCCategoryTables.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "FGScannableSubsystem.h"
#include "FGWorldScannableData.h"
#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "FGDropPod.h"

void VTC::GatherRegistry(UWorld* World, const FVTCCategoryTables& Tables,
	TMap<FGuid, FVTCCandidate>& Out)
{
	if (!World || World->GetNetMode() == NM_Client) return;
	AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(World);
	if (!Scan) return;

	auto Gather = [&](const TArray<FWorldScannableData>& Records, bool IsPod)
	{
		for (const FWorldScannableData& Record : Records)
		{
			if (!Record.ActorGuid.IsValid() ||
				(IsPod ? Scan->HasDropPodBeenLooted(Record.ActorGuid) : !Scan->DoesPickupExist(Record.ActorGuid)))
				continue;
			AActor* Actor = Record.Actor.Get();
			if (Actor && Actor->IsActorBeingDestroyed()) continue;
			if (const AFGItemPickup* Pickup = Cast<AFGItemPickup>(Actor); Pickup && Pickup->IsPickedUp()) continue;
			if (const AFGDropPod* Pod = Cast<AFGDropPod>(Actor); Pod && Pod->HasBeenLooted()) continue;
			EVTCCollectibleCategory Category;
			if (!Tables.Resolve(Actor, Record.ActorClass.Get(), Category)) continue;
			FVTCCandidate Candidate;
			Candidate.Data.Id = Record.ActorGuid;
			Candidate.Data.Location = Actor ? Actor->GetActorLocation() : Record.ActorLocation;
			Candidate.Data.Category = static_cast<uint8>(Category);
			Candidate.Actor = Actor;
			Out.Add(Candidate.Data.Id, Candidate);
		}
	};
	Gather(Scan->GetAvailableItemPickups(), false);
	Gather(Scan->GetAvailableDropPods(), true);
}

void VTC::GatherLoaded(UWorld* World, const FVTCCategoryTables& Tables,
	TMap<TWeakObjectPtr<AActor>, FGuid>& RuntimeIds, TMap<FGuid, FVTCCandidate>& Out,
	FVTCCollectionState& CollectionState, bool bRequireServerRecord)
{
	AFGScannableSubsystem* Scan = World->GetNetMode() != NM_Client ? AFGScannableSubsystem::Get(World) : nullptr;
	auto Add = [&](AActor* Actor, FGuid Id, bool Collected, bool CanRespawn, bool IsRuntime, bool IsPod)
	{
		if (!IsValid(Actor)) return;
		if (IsRuntime || !Id.IsValid())
		{
			FGuid& LocalId = RuntimeIds.FindOrAdd(Actor);
			if (!LocalId.IsValid()) LocalId = FGuid::NewGuid();
			Id = LocalId;
		}
		else if (Scan)
		{
			Collected |= IsPod ? Scan->HasDropPodBeenLooted(Id) : !Scan->DoesPickupExist(Id);
		}
		FVTCCandidate Candidate;
		Candidate.Data.Id = Id;
		Candidate.Actor = Actor;
		// Empty pickups can no longer resolve their descriptor. Record collection before
		// category resolution so those actors still suppress an older feed entry.
		if (Collected)
		{
			CollectionState.MergeLoaded(Out, Candidate, true, CanRespawn, IsRuntime, bRequireServerRecord);
			return;
		}
		if (Actor->IsActorBeingDestroyed())
		{
			Out.Remove(Id);
			return;
		}
		EVTCCollectibleCategory Category;
		if (!Tables.Resolve(Actor, Actor->GetClass(), Category)) return;
		Candidate.Data.Location = Actor->GetActorLocation();
		Candidate.Data.Category = static_cast<uint8>(Category);
		CollectionState.MergeLoaded(Out, Candidate, false, CanRespawn, IsRuntime, bRequireServerRecord);
	};
	for (TActorIterator<AFGItemPickup> It(World); It; ++It)
	{
		Add(*It, It->GetItemPickupGuid(), It->IsPickedUp(), It->CanEverRespawn(), It->IsA<AFGItemPickup_Spawnable>(), false);
	}
	for (TActorIterator<AFGDropPod> It(World); It; ++It)
	{
		Add(*It, It->GetDropPodGuid(), It->HasBeenLooted(), false, false, true);
	}
	for (auto It = RuntimeIds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			CollectionState.ForgetRuntimeItem(It.Value());
			It.RemoveCurrent();
		}
	}
	CollectionState.Filter(Out);
}
