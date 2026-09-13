#include "VTCServerFeedSubsystem.h"

#include "VTCCollectibleFeedComponent.h"
#include "VTCConfig.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "FGScannableSubsystem.h"
#include "FGWorldScannableData.h"

DEFINE_LOG_CATEGORY_STATIC(LogVTCServerFeed, Log, All);

namespace
{
	// How often the per-player feed is recomputed. Coarser than the client outline refresh —
	// these are distant collectibles, a second or two of lag is invisible.
	constexpr float FeedRefreshSeconds = 1.5f;
}

bool UVTCServerFeedSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return true;
}

void UVTCServerFeedSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tables.Load();
}

void UVTCServerFeedSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	Super::Deinitialize();
}

void UVTCServerFeedSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Only meaningful where there are remote clients to feed.
	const ENetMode NetMode = InWorld.GetNetMode();
	if (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer)
	{
		return;
	}

	InWorld.GetTimerManager().SetTimer(
		RefreshTimerHandle, this, &UVTCServerFeedSubsystem::RefreshFeeds, FeedRefreshSeconds, /*loop*/ true);
}

void UVTCServerFeedSubsystem::RefreshFeeds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(this);
	if (!Scan)
	{
		return;
	}

	// Only used as a fallback for a player who hasn't reported their own preference yet (e.g.
	// right after connecting) — see PlayerFeedDistSq/PlayerMaxEntries below, sourced from each
	// player's own Server_ReportRangePreference instead once available.
	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	const double FeedDistSq = FMath::Square(Cfg.MaxDistanceCm());

	auto EnsureComponent = [](APlayerController* PC) -> UVTCCollectibleFeedComponent*
	{
		if (UVTCCollectibleFeedComponent* Existing = PC->FindComponentByClass<UVTCCollectibleFeedComponent>())
		{
			return Existing;
		}
		UVTCCollectibleFeedComponent* Comp = NewObject<UVTCCollectibleFeedComponent>(PC);
		Comp->RegisterComponent();
		Comp->SetIsReplicated(true);   // also registers it in the PC's replicated-components list
		return Comp;
	};

	// Master list of live collectibles: position + category, built once per refresh.
	struct FEntry { FVector Location; uint8 Category; };
	TArray<FEntry> All;
	All.Reserve(256);

	int32 PickupLive = 0;
	int32 PodLive = 0;
	auto Gather = [&](const TArray<FWorldScannableData>& Data, bool bDropPods)
	{
		for (const FWorldScannableData& D : Data)
		{
			if (bDropPods ? Scan->HasDropPodBeenLooted(D.ActorGuid) : !Scan->DoesPickupExist(D.ActorGuid))
			{
				continue;
			}
			bDropPods ? ++PodLive : ++PickupLive;
			EVTCCollectibleCategory Category;
			if (Tables.Resolve(D.Actor.Get(), D.ActorClass.Get(), Category))
			{
				All.Add({ D.ActorLocation, static_cast<uint8>(Category) });
			}
		}
	};
	Gather(Scan->GetAvailableItemPickups(), /*bDropPods*/ false);
	Gather(Scan->GetAvailableDropPods(), /*bDropPods*/ true);

	// Log the collected/looted filtering counts once, and again whenever they change - a
	// looted pod or picked-up item that never drops out of "live" points at
	// HasDropPodBeenLooted()/DoesPickupExist() not reflecting collection on this server.
	const int32 PickupTotal = Scan->GetAvailableItemPickups().Num();
	const int32 PodTotal = Scan->GetAvailableDropPods().Num();
	if (PickupTotal != LastLoggedPickupTotal || PickupLive != LastLoggedPickupLive ||
		PodTotal != LastLoggedPodTotal || PodLive != LastLoggedPodLive)
	{
		LastLoggedPickupTotal = PickupTotal;
		LastLoggedPickupLive = PickupLive;
		LastLoggedPodTotal = PodTotal;
		LastLoggedPodLive = PodLive;
		UE_LOG(LogVTCServerFeed, Display,
			TEXT("RefreshFeeds: pickups %d/%d live (not collected), pods %d/%d live (not looted)"),
			PickupLive, PickupTotal, PodLive, PodTotal);
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		UVTCCollectibleFeedComponent* Comp = EnsureComponent(PC);

		const APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			Comp->SetFeed(TArray<FVTCFedCollectible>());
			continue;
		}
		const FVector PlayerLoc = Pawn->GetActorLocation();

		// This player's own Max Distance/Max Simultaneous, reported by their client — each
		// machine has its own copy of the mod's config, so the server's own Cfg is meaningless
		// for deciding what THIS player asked to see. Fall back to it only until their first
		// report arrives (e.g. the moment right after connecting).
		const double PlayerFeedDistSq = Comp->GetDesiredMaxDistanceCm() > 0.f
			? FMath::Square(static_cast<double>(Comp->GetDesiredMaxDistanceCm()))
			: FeedDistSq;
		const int32 PlayerMaxEntries = Comp->GetDesiredMaxEntries() > 0
			? Comp->GetDesiredMaxEntries()
			: Cfg.SafeMaxSimultaneous();

		// Gather everything in range with its distance, nearest first, THEN cap — capping
		// without sorting first would just take whatever happens to be first in the registry's
		// storage order (grouped by level, not by proximity), silently dropping truly nearby
		// entries at large Max Distance values in favour of arbitrary ones.
		TArray<TPair<double, const FEntry*>> InRange;
		InRange.Reserve(All.Num());
		for (const FEntry& E : All)
		{
			const double DistSq = FVector::DistSquared(E.Location, PlayerLoc);
			if (DistSq <= PlayerFeedDistSq)
			{
				InRange.Add({ DistSq, &E });
			}
		}
		InRange.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });

		// The cap exists to bound per-tick replication bandwidth, not to second-guess how many
		// the player asked to see — so it never sits below what their own config wants.
		const int32 Cap = FMath::Max(MaxEntriesPerPlayer, PlayerMaxEntries);

		TArray<FVTCFedCollectible> Feed;
		Feed.Reserve(FMath::Min(InRange.Num(), Cap));
		for (const TPair<double, const FEntry*>& Entry : InRange)
		{
			if (Feed.Num() >= Cap)
			{
				break;
			}
			FVTCFedCollectible F;
			F.Location = Entry.Value->Location;
			F.Category = Entry.Value->Category;
			Feed.Add(F);
		}
		const int32 Sent = Feed.Num();
		const int32 InRangeCount = InRange.Num();
		Comp->SetFeed(MoveTemp(Feed));

		if (PlayerFeedDistSq != LastLoggedFeedDistSq || Cap != LastLoggedCap ||
			InRangeCount != LastLoggedInRange || Sent != LastLoggedFeedSent)
		{
			LastLoggedFeedDistSq = PlayerFeedDistSq;
			LastLoggedCap = Cap;
			LastLoggedInRange = InRangeCount;
			LastLoggedFeedSent = Sent;
			UE_LOG(LogVTCServerFeed, Display,
				TEXT("feed for %s: distance=%.0fm (reported=%d) cap=%d -> %d in range, %d sent"),
				*PC->GetName(), FMath::Sqrt(PlayerFeedDistSq) / 100.0,
				Comp->GetDesiredMaxDistanceCm() > 0.f ? 1 : 0, Cap, InRangeCount, Sent);
		}
	}
}
