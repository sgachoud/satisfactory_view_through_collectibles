#include "VTCServerFeedSubsystem.h"
#include "VTCCollectibleFeedComponent.h"
#include "VTCDiscovery.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "FGPlayerController.h"
#include "Player/SMLRemoteCallObject.h"

bool UVTCServerFeedSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UVTCServerFeedSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tables.Load();
}

void UVTCServerFeedSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
				if (auto* Comp = PC->FindComponentByClass<UVTCCollectibleFeedComponent>())
					Comp->DestroyComponent();
		}
	}
	Super::Deinitialize();
}

void UVTCServerFeedSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer || InWorld.GetNetMode() == NM_ListenServer)
	{
		// Scheduling resolution matches the minimum supported client refresh interval.
		InWorld.GetTimerManager().SetTimer(RefreshTimerHandle, this,
			&UVTCServerFeedSubsystem::RefreshFeeds, 0.1f, true);
	}
}

void UVTCServerFeedSubsystem::RefreshFeeds()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;
	const double Now = World->GetTimeSeconds();
	TArray<UVTCCollectibleFeedComponent*> Due;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AFGPlayerController* PC = Cast<AFGPlayerController>(It->Get());
		if (!PC || PC->IsLocalController()) continue;
		const USMLRemoteCallObject* SML = PC->GetRemoteCallObjectOfClass<USMLRemoteCallObject>();
		// Never replicate an unknown component class to clients without this mod.
		if (!SML || !SML->IsClientModInstalled(TEXT("ViewThroughCollectibles"))) continue;
		const FVersion Version = SML->GetClientModVersion(TEXT("ViewThroughCollectibles"));
		if (Version.Major != 0 || Version.Minor != 2) continue;

		auto* Comp = PC->FindComponentByClass<UVTCCollectibleFeedComponent>();
		if (!Comp)
		{
			Comp = NewObject<UVTCCollectibleFeedComponent>(PC, NAME_None, RF_Transient);
			Comp->RegisterComponent();
		}
		if (Comp->IsRefreshDue(Now)) Due.Add(Comp);
	}
	if (Due.IsEmpty()) return;

	TMap<FGuid, FVTCCandidate> Registry;
	const bool NeedsRegistry = Due.ContainsByPredicate([](const UVTCCollectibleFeedComponent* Comp)
	{
		const auto& Preferences = Comp->GetPreferences();
		return Preferences.MaxEntries > 0 && Preferences.MaxDistanceCm > 0 && Preferences.EnabledCategories != 0;
	});
	if (NeedsRegistry) VTC::GatherRegistry(World, Tables, Registry);
	for (UVTCCollectibleFeedComponent* Comp : Due)
	{
		const APlayerController* PC = Cast<APlayerController>(Comp->GetOwner());
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		TArray<FVTCFedCollectible> Entries;
		if (Pawn)
		{
			const auto Selected = VTC::SelectCollectibles(Registry, Pawn->GetActorLocation(), Comp->GetPreferences());
			Entries.Reserve(Selected.Num());
			for (const FVTCCandidate& Candidate : Selected) Entries.Add(Candidate.Data);
			// Stable wire order avoids updates merely because distances exchange rank.
			Entries.Sort([](const auto& A, const auto& B) { return VTC::IdLess(A.Id, B.Id); });
		}
		Comp->SetFeed(MoveTemp(Entries), Now);
	}
}