#include "VTCServerFeedSubsystem.h"

#include "VTCCollectibleFeedComponent.h"
#include "VTCConfig.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "FGScannableSubsystem.h"
#include "FGWorldScannableData.h"

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

	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	// Feed the same range the client outlines at — markers just stand in for outlines the
	// client can't draw yet.
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

	auto Gather = [&](const TArray<FWorldScannableData>& Data, bool bDropPods)
	{
		for (const FWorldScannableData& D : Data)
		{
			if (bDropPods ? Scan->HasDropPodBeenLooted(D.ActorGuid) : !Scan->DoesPickupExist(D.ActorGuid))
			{
				continue;
			}
			EVTCCollectibleCategory Category;
			if (Tables.Resolve(D.Actor.Get(), D.ActorClass.Get(), Category))
			{
				All.Add({ D.ActorLocation, static_cast<uint8>(Category) });
			}
		}
	};
	Gather(Scan->GetAvailableItemPickups(), /*bDropPods*/ false);
	Gather(Scan->GetAvailableDropPods(), /*bDropPods*/ true);

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

		TArray<FVTCFedCollectible> Feed;
		for (const FEntry& E : All)
		{
			if (FVector::DistSquared(E.Location, PlayerLoc) > FeedDistSq)
			{
				continue;
			}
			FVTCFedCollectible F;
			F.Location = E.Location;
			F.Category = E.Category;
			Feed.Add(F);
			if (Feed.Num() >= MaxEntriesPerPlayer)
			{
				break;
			}
		}
		Comp->SetFeed(MoveTemp(Feed));
	}
}
