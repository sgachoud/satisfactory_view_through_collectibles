#include "VTCOutlineSubsystem.h"

#include "VTCCollectibleFeedComponent.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"

// FactoryGame. Header paths verified against SML 3.12 / FactoryGame CL 502094.
#include "FGScannableSubsystem.h"
#include "FGWorldScannableData.h"
#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "FGDropPod.h"
#include "FGActorRepresentationManager.h"
#include "FGActorRepresentation.h"
#include "VTCMapRepresentation.h"

DEFINE_LOG_CATEGORY_STATIC(LogViewThroughCollectibles, Log, All);

const TCHAR* UVTCOutlineSubsystem::OutlineMaterialPath =
	TEXT("/ViewThroughCollectibles/Materials/M_VTCOutline.M_VTCOutline");
const TCHAR* UVTCOutlineSubsystem::ColorCollectionPath =
	TEXT("/ViewThroughCollectibles/Materials/MPC_VTCColors.MPC_VTCColors");

// Engine primitive used for the "distant collectible" marker. Satisfactory cooks
// /Engine/BasicShapes (the game itself references Cylinder as a marker mesh).
static const TCHAR* MarkerMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

// ---------------------------------------------------------------------------------------------

bool UVTCOutlineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (IsRunningDedicatedServer())
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return true;
}

void UVTCOutlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tables.Load();
}

void UVTCOutlineSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	ClearAllTrackedOutlines();
	ClearRemoteMarkers();
	ClearMapDots();
	RestoreCustomDepthJitterOverride();

	if (IsValid(PostProcessVolume))
	{
		PostProcessVolume->Destroy();
	}
	PostProcessVolume = nullptr;
	OutlineMID = nullptr;

	Super::Deinitialize();
}

void UVTCOutlineSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	ApplyCustomDepthJitterOverride();
	SetupPostProcess();

	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	PushConfigToMaterial(Cfg);

	// The mod config subsystem may not be populated this early, so Cfg here can still be
	// C++ defaults. RefreshOutlines re-arms from the live config once it changes.
	ArmedRefreshInterval = Cfg.SafeRefreshIntervalSeconds();
	InWorld.GetTimerManager().SetTimer(
		RefreshTimerHandle, this, &UVTCOutlineSubsystem::RefreshOutlines, ArmedRefreshInterval, /*loop*/ true);

	RefreshOutlines();
}

// ---------------------------------------------------------------------------------------------

void UVTCOutlineSubsystem::SetupPostProcess()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ColorCollection = LoadObject<UMaterialParameterCollection>(nullptr, ColorCollectionPath);
	if (!ColorCollection)
	{
		UE_LOG(LogViewThroughCollectibles, Error,
			TEXT("Could not load MPC at %s - outlines will be uncoloured."), ColorCollectionPath);
	}

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, OutlineMaterialPath);
	if (!BaseMat)
	{
		UE_LOG(LogViewThroughCollectibles, Error,
			TEXT("Could not load outline material at %s - mod disabled for this session."),
			OutlineMaterialPath);
		return;
	}

	OutlineMID = UMaterialInstanceDynamic::Create(BaseMat, this);

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient; // never serialised into the Satisfactory save
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PostProcessVolume = World->SpawnActor<APostProcessVolume>(SpawnParams);
	if (!PostProcessVolume)
	{
		UE_LOG(LogViewThroughCollectibles, Error, TEXT("Failed to spawn post-process volume."));
		return;
	}
	PostProcessVolume->bUnbound = true;
	PostProcessVolume->BlendWeight = 1.f;
	PostProcessVolume->Priority = 1000000.f; // sit on top of the game's own volumes
	PostProcessVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, OutlineMID));
}

void UVTCOutlineSubsystem::LogConfigIfChanged(const FVTCConfigStruct& Cfg)
{
	FString Digest = FString::Printf(
		TEXT("dist=%g refresh=%g maxN=%d thick=%g fill=%g map=%d |"),
		Cfg.MaxDistanceMeters, Cfg.RefreshIntervalSeconds, Cfg.MaxSimultaneousOutlines,
		Cfg.OutlineThicknessPixels, Cfg.OccludedFillOpacity, Cfg.ShowOnMap ? 1 : 0);
	for (uint8 i = 0; i < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++i)
	{
		const FVTCTypeSettings& T = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(i));
		Digest += FString::Printf(TEXT(" %d:%d/%s"), i, T.Enabled ? 1 : 0, *T.Color);
	}

	if (Digest != LastLoggedConfigDigest)
	{
		LastLoggedConfigDigest = Digest;
		UE_LOG(LogViewThroughCollectibles, Display, TEXT("effective config -> %s"), *Digest);
	}
}

void UVTCOutlineSubsystem::PushConfigToMaterial(const FVTCConfigStruct& Cfg)
{
	if (!ColorCollection)
	{
		return;
	}

	// Skip if nothing relevant changed since last push.
	if (bConfigEverPushed)
	{
		bool bChanged =
			!FMath::IsNearlyEqual(Cfg.OutlineThicknessPixels, LastPushedConfig.OutlineThicknessPixels) ||
			!FMath::IsNearlyEqual(Cfg.OccludedFillOpacity, LastPushedConfig.OccludedFillOpacity);
		for (uint8 i = 0; !bChanged && i < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++i)
		{
			const FVTCTypeSettings& A = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(i));
			const FVTCTypeSettings& B = LastPushedConfig.GetFor(static_cast<EVTCCollectibleCategory>(i));
			bChanged = A.Enabled != B.Enabled || !A.Color.Equals(B.Color, ESearchCase::IgnoreCase);
		}
		if (!bChanged)
		{
			return;
		}
	}

	for (uint8 i = 0; i < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++i)
	{
		const FVTCTypeSettings& T = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(i));
		// Colour param "Color<stencil>" so the material can index by stencil value directly.
		const FName ParamName(*FString::Printf(TEXT("Color%d"), StencilFor(static_cast<EVTCCollectibleCategory>(i))));
		FLinearColor Value = T.GetLinearColor();
		Value.A = T.Enabled ? 1.f : 0.f; // alpha 0 => material draws nothing for this stencil
		UKismetMaterialLibrary::SetVectorParameterValue(this, ColorCollection, ParamName, Value);
	}

	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OutlineThicknessPixels"), Cfg.OutlineThicknessPixels);
	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OccludedFillOpacity"), Cfg.OccludedFillOpacity);

	LastPushedConfig = Cfg;
	bConfigEverPushed = true;
}

void UVTCOutlineSubsystem::ApplyCustomDepthJitterOverride()
{
	if (SavedCustomDepthJitter != MIN_int32)
	{
		return; // already applied
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")))
	{
		SavedCustomDepthJitter = CVar->GetInt();
		// SetByGameOverride so the user can still override it from the console if they want.
		CVar->Set(0, ECVF_SetByGameOverride);
	}
}

void UVTCOutlineSubsystem::RestoreCustomDepthJitterOverride()
{
	if (SavedCustomDepthJitter == MIN_int32)
	{
		return; // never touched it
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")))
	{
		CVar->Set(SavedCustomDepthJitter, ECVF_SetByGameOverride);
	}
	SavedCustomDepthJitter = MIN_int32;
}

// ---------------------------------------------------------------------------------------------

namespace
{
	struct FCandidate
	{
		TWeakObjectPtr<AActor> Actor;
		EVTCCollectibleCategory Category;
		double DistSq;
	};
}

void UVTCOutlineSubsystem::RefreshOutlines()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	if (!LocalPawn)
	{
		ClearAllTrackedOutlines();
		return;
	}

	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	LogConfigIfChanged(Cfg);
	PushConfigToMaterial(Cfg);

	// Re-arm the refresh timer if the configured interval changed (it's set live from the
	// menu, and the value at OnWorldBeginPlay may have been a default).
	const float DesiredInterval = Cfg.SafeRefreshIntervalSeconds();
	if (!FMath::IsNearlyEqual(DesiredInterval, ArmedRefreshInterval))
	{
		ArmedRefreshInterval = DesiredInterval;
		World->GetTimerManager().SetTimer(
			RefreshTimerHandle, this, &UVTCOutlineSubsystem::RefreshOutlines, DesiredInterval, /*loop*/ true);
	}

	const FVector PlayerLoc = LocalPawn->GetActorLocation();
	const double MaxDistSq = FMath::Square(Cfg.MaxDistanceCm());
	const int32 MaxOutlines = Cfg.SafeMaxSimultaneous();

	// Remote client, server also running the mod: an ephemeral feed component is replicated
	// onto our PlayerController. It carries the authoritative, collected-filtered set, so when
	// it's present we must NOT fall back to the map's raw scan data (which can't tell what
	// other players have already picked up).
	UVTCCollectibleFeedComponent* FeedComp =
		PC ? PC->FindComponentByClass<UVTCCollectibleFeedComponent>() : nullptr;
	const bool bHasServerFeed = FeedComp != nullptr;

	// Tell the server what WE want to see - it has no other way to know our own Max
	// Distance/Max Simultaneous, only its own local config. Only send when it actually
	// changes; this runs every refresh and the RPC is reliable.
	if (FeedComp)
	{
		const float DesiredDistCm = Cfg.MaxDistanceCm();
		const int32 DesiredMaxEntries = MaxOutlines;
		if (!FMath::IsNearlyEqual(DesiredDistCm, LastReportedMaxDistanceCm) ||
			DesiredMaxEntries != LastReportedMaxEntries)
		{
			LastReportedMaxDistanceCm = DesiredDistCm;
			LastReportedMaxEntries = DesiredMaxEntries;
			FeedComp->Server_ReportRangePreference(DesiredDistCm, DesiredMaxEntries);
		}
	}

	TArray<FCandidate> Candidates;
	// Nearby collectibles from the registry, streamed or not — the marker pass reuses this
	// so the registry is walked once per refresh, not twice.
	TArray<FVTCFedCollectible> RegistryNearby;

	auto Consider = [&](AActor* Actor, const UClass* FallbackClass, const FVector& Loc)
	{
		// We can only write Custom Depth on a streamed-in actor, so skip anything not loaded.
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			return;
		}
		const double D = FVector::DistSquared(Loc, PlayerLoc);
		if (D > MaxDistSq)
		{
			return;
		}
		EVTCCollectibleCategory Category;
		if (!Tables.Resolve(Actor, FallbackClass, Category))
		{
			return;
		}
		if (!Cfg.GetFor(Category).Enabled)
		{
			return;
		}
		Candidates.Add({ Actor, Category, D });
	};

	// --- Level-placed collectibles via the game's own scannable registry -------------------
	// AFGScannableSubsystem's position arrays (mAvailableItemPickups/mAvailableDropPods) get
	// populated by a level-placed generator actor on EVERY machine, client included — but its
	// collected-state sets (mDestroyedPickups/mLootedDropPods) are SaveGame data that only
	// loads when we load a save ourselves. A remote client never does, so on a client those
	// stay empty forever and DoesPickupExist()/HasDropPodBeenLooted() silently claim nothing
	// is collected. So this registry is only trustworthy where we're actually authoritative
	// (solo or listen host) — gate on net mode, not on whether the arrays happen to have data.
	const bool bIsAuthority = World->GetNetMode() != NM_Client;
	int32 ScannableHits = 0;
	if (AFGScannableSubsystem* Scan = bIsAuthority ? AFGScannableSubsystem::Get(this) : nullptr)
	{
		auto Scan1 = [&](const FWorldScannableData& Data)
		{
			++ScannableHits;
			const double D = FVector::DistSquared(Data.ActorLocation, PlayerLoc);
			if (D > MaxDistSq)
			{
				return;
			}
			AActor* Actor = Data.Actor.Get();
			EVTCCollectibleCategory Category;
			if (!Tables.Resolve(Actor, Data.ActorClass.Get(), Category))
			{
				return;
			}
			if (!Cfg.GetFor(Category).Enabled)
			{
				return;
			}
			FVTCFedCollectible F;
			F.Location = Data.ActorLocation;
			F.Category = static_cast<uint8>(Category);
			RegistryNearby.Add(F);
			// Also outline it if it's streamed in.
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Candidates.Add({ Actor, Category, D });
			}
		};
		for (const FWorldScannableData& Data : Scan->GetAvailableItemPickups())
		{
			if (Scan->DoesPickupExist(Data.ActorGuid))
			{
				Scan1(Data);
			}
		}
		for (const FWorldScannableData& Data : Scan->GetAvailableDropPods())
		{
			if (!Scan->HasDropPodBeenLooted(Data.ActorGuid))
			{
				Scan1(Data);
			}
		}
	}

	// Fallback for a remote client on a server WITHOUT the mod (registry arrays above are
	// Transient so empty here, and there's no feed component). AFGWorldScannableDataGenerator
	// is a cooked level actor present on clients too; it lists every level-placed pickup/pod
	// with position + class, including ones not streamed in — so this covers far-away
	// collectibles. It can't know what other players already collected, hence feed first.
	int32 GeneratorHits = 0;
	if (ScannableHits == 0 && !bHasServerFeed)
	{
		auto FromGenerator = [&](const FWorldScannableData& Data, bool bIsPod)
		{
			const double D = FVector::DistSquared(Data.ActorLocation, PlayerLoc);
			if (D > MaxDistSq)
			{
				return;
			}
			AActor* Actor = Data.Actor.Get();
			// Collected/looted state is only knowable when the actor is streamed in; when it
			// isn't, show it (matches how the game's own object scanner treats them).
			if (IsValid(Actor))
			{
				if (bIsPod)
				{
					const AFGDropPod* Pod = Cast<AFGDropPod>(Actor);
					if (Pod && Pod->HasBeenLooted())
					{
						return;
					}
				}
				else if (const AFGItemPickup* Pickup = Cast<AFGItemPickup>(Actor))
				{
					if (Pickup->IsPickedUp())
					{
						return;
					}
				}
			}
			EVTCCollectibleCategory Category;
			if (!Tables.Resolve(Actor, Data.ActorClass.Get(), Category))
			{
				return;
			}
			if (!Cfg.GetFor(Category).Enabled)
			{
				return;
			}
			FVTCFedCollectible F;
			F.Location = Data.ActorLocation;
			F.Category = static_cast<uint8>(Category);
			RegistryNearby.Add(F);
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Candidates.Add({ Actor, Category, D });
			}
		};
		for (TActorIterator<AFGWorldScannableDataGenerator> It(World); It; ++It)
		{
			for (const FWorldScannableData& Data : It->mItemPickups) { ++GeneratorHits; FromGenerator(Data, /*bIsPod*/ false); }
			for (const FWorldScannableData& Data : It->mDropPods)     { ++GeneratorHits; FromGenerator(Data, /*bIsPod*/ true); }
		}
	}

	if (ScannableHits == 0 && GeneratorHits == 0)
	{
		// Last resort: sweep streamed-in actors directly.
		for (TActorIterator<AFGItemPickup> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsPickedUp())
			{
				Consider(*It, It->GetClass(), It->GetActorLocation());
			}
		}
		for (TActorIterator<AFGDropPod> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->HasBeenLooted())
			{
				Consider(*It, It->GetClass(), It->GetActorLocation());
			}
		}
	}

	// Log the active source, and again whenever it changes (e.g. the feed component arriving
	// a few seconds after joining a modded server).
	const int32 SourceSig = (ScannableHits > 0 ? 1 : 0) | (bHasServerFeed ? 2 : 0) | (GeneratorHits > 0 ? 4 : 0);
	if (SourceSig != LastSourceSig)
	{
		LastSourceSig = SourceSig;
		UE_LOG(LogViewThroughCollectibles, Display,
			TEXT("collectible source: scannableHits=%d serverFeed=%d generatorHits=%d -> %d nearby in range"),
			ScannableHits, bHasServerFeed ? 1 : 0, GeneratorHits, RegistryNearby.Num());
	}

	// --- Runtime player-dropped items (never in the scannable registry) -------------------
	for (TActorIterator<AFGItemPickup_Spawnable> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsPickedUp())
		{
			Consider(*It, It->GetClass(), It->GetActorLocation());
		}
	}

	// --- Keep the nearest N that are actually streamed in --------------------------------
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistSq < B.DistSq; });

	TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory> Desired;
	for (const FCandidate& C : Candidates)
	{
		if (Desired.Num() >= MaxOutlines)
		{
			break;
		}
		AActor* Actor = C.Actor.Get();
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
		{
			Desired.Add(Actor, C.Category);
		}
	}

	// --- Diff against what is currently written --------------------------------------------
	// Clear outlines for actors no longer wanted (out of range, collected, gone).
	for (auto It = TrackedOutlines.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		if (!IsValid(Actor) || !Desired.Contains(Actor))
		{
			if (IsValid(Actor))
			{
				ApplyCustomDepth(Actor, 0, false);
			}
			It.RemoveCurrent();
		}
	}
	// (Re)apply the stencil for everything wanted, every refresh. The component setters are
	// guarded so this is near-free when already correct, and re-running it catches meshes
	// that were built or streamed in *after* the actor was first tracked — e.g. a
	// just-dropped item whose mesh isn't ready on the first pass, or a changed category.
	for (const TPair<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& Pair : Desired)
	{
		if (AActor* Actor = Pair.Key.Get(); IsValid(Actor))
		{
			ApplyCustomDepth(Actor, StencilFor(Pair.Value), true);
			TrackedOutlines.Add(Actor, Pair.Value);
		}
	}

	// --- Distant collectibles (registry or server feed) we haven't streamed in ------------
	RefreshRemoteMarkers(Cfg, PlayerLoc, Desired, RegistryNearby,
		/*bHaveRegistry*/ ScannableHits > 0 || GeneratorHits > 0);

	// --- Map / compass dots ---------------------------------------------------------------
	RefreshMapDots(Cfg, Desired);
}

// ---------------------------------------------------------------------------------------------

void UVTCOutlineSubsystem::ApplyCustomDepth(AActor* Actor, int32 StencilValue, bool bEnable)
{
	if (!IsValid(Actor))
	{
		return;
	}
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh))
		{
			continue;
		}
		Mesh->SetRenderCustomDepth(bEnable);
		if (bEnable)
		{
			Mesh->SetCustomDepthStencilValue(StencilValue);
		}
	}
}

void UVTCOutlineSubsystem::ClearAllTrackedOutlines()
{
	for (const TPair<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& Pair : TrackedOutlines)
	{
		if (AActor* Actor = Pair.Key.Get())
		{
			ApplyCustomDepth(Actor, 0, false);
		}
	}
	TrackedOutlines.Reset();
}

// ---------------------------------------------------------------------------------------------

namespace
{
	// Stable key for a fed collectible: 1 m-quantised world position (20 bits/axis) + category
	// (low 4 bits). Collides only for collectibles ~1048 km apart on an axis.
	uint64 MarkerKey(const FVector& Loc, uint8 Category)
	{
		const uint64 X = static_cast<uint64>(static_cast<uint32>(FMath::RoundToInt(Loc.X / 100.0))) & 0xFFFFF;
		const uint64 Y = static_cast<uint64>(static_cast<uint32>(FMath::RoundToInt(Loc.Y / 100.0))) & 0xFFFFF;
		const uint64 Z = static_cast<uint64>(static_cast<uint32>(FMath::RoundToInt(Loc.Z / 100.0))) & 0xFFFFF;
		return (static_cast<uint64>(Category) & 0xF) | (X << 4) | (Y << 24) | (Z << 44);
	}

	// The category's UMETA(DisplayName) — used as the marker's map/compass label.
	FText CategoryLabel(EVTCCollectibleCategory Category)
	{
		if (const UEnum* Enum = StaticEnum<EVTCCollectibleCategory>())
		{
			return Enum->GetDisplayNameTextByValue(static_cast<int64>(Category));
		}
		return FText::GetEmpty();
	}
}

void UVTCOutlineSubsystem::RefreshRemoteMarkers(const FVTCConfigStruct& Cfg, const FVector& PlayerLoc,
	const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines,
	const TArray<FVTCFedCollectible>& RegistryNearby, bool bHaveRegistry)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!MarkerMesh)
	{
		MarkerMesh = LoadObject<UStaticMesh>(nullptr, MarkerMeshPath);
		if (!MarkerMesh)
		{
			UE_LOG(LogViewThroughCollectibles, Warning,
				TEXT("Marker mesh %s not found - distant-collectible markers disabled."), MarkerMeshPath);
			ClearRemoteMarkers();
			return;
		}
	}

	// Positions: the registry's nearby set (already gathered by RefreshOutlines) when we're
	// the authority; otherwise the server-fed list on our PlayerController.
	TArray<FVTCFedCollectible> FeedCopy;
	const TArray<FVTCFedCollectible>* Candidates = &RegistryNearby;
	if (!bHaveRegistry)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const UVTCCollectibleFeedComponent* Feed =
			PC ? PC->FindComponentByClass<UVTCCollectibleFeedComponent>() : nullptr;
		if (!Feed)
		{
			ClearRemoteMarkers();
			return;
		}
		FeedCopy = Feed->GetFeed();
		Candidates = &FeedCopy;
	}

	const double MarkerDistSq = FMath::Square(Cfg.MaxDistanceCm());
	// A candidate this close to one we're already outlining is the same object — skip it.
	const double DedupeDistSq = FMath::Square(400.0);

	// Cache the positions of what we're already outlining, to suppress duplicate markers.
	TArray<FVector, TInlineAllocator<64>> OutlinedLocs;
	for (const TPair<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& Pair : LoadedOutlines)
	{
		if (const AActor* A = Pair.Key.Get())
		{
			OutlinedLocs.Add(A->GetActorLocation());
		}
	}

	// Filter to markers we want this tick, nearest first, capped to the outline budget.
	struct FWantedMarker { uint64 Key; FVector Loc; EVTCCollectibleCategory Category; double DistSq; };
	TArray<FWantedMarker> WantedList;
	for (const FVTCFedCollectible& Entry : *Candidates)
	{
		if (Entry.Category >= static_cast<uint8>(EVTCCollectibleCategory::MAX))
		{
			continue;
		}
		const EVTCCollectibleCategory Category = static_cast<EVTCCollectibleCategory>(Entry.Category);
		if (!Cfg.GetFor(Category).Enabled)
		{
			continue;
		}
		const FVector Loc = Entry.Location;
		const double DistSq = FVector::DistSquared(Loc, PlayerLoc);
		if (DistSq > MarkerDistSq)
		{
			continue;
		}
		bool bAlreadyOutlined = false;
		for (const FVector& OL : OutlinedLocs)
		{
			if (FVector::DistSquared(OL, Loc) <= DedupeDistSq)
			{
				bAlreadyOutlined = true;
				break;
			}
		}
		if (bAlreadyOutlined)
		{
			continue;
		}
		WantedList.Add({ MarkerKey(Loc, Entry.Category), Loc, Category, DistSq });
	}

	WantedList.Sort([](const FWantedMarker& A, const FWantedMarker& B) { return A.DistSq < B.DistSq; });
	const int32 MaxMarkers = Cfg.SafeMaxSimultaneous();
	if (WantedList.Num() > MaxMarkers)
	{
		WantedList.SetNum(MaxMarkers);
	}

	// Spawning is real Actor/Component creation cost — capping how many NEW markers a single
	// refresh may create keeps a fresh join (or a big jump in Max Distance, which can put
	// hundreds at once into WantedList) from creating them all in one frame and hitching. The
	// rest just get picked up on the following refreshes until the full budget is filled.
	constexpr int32 MaxNewMarkersPerRefresh = 20;
	int32 NewlySpawned = 0;

	TSet<uint64> Wanted;
	for (const FWantedMarker& W : WantedList)
	{
		const uint64 Key = W.Key;
		const FVector Loc = W.Loc;
		const EVTCCollectibleCategory Category = W.Category;
		Wanted.Add(Key);
		if (RemoteMarkers.Contains(Key))
		{
			continue;
		}
		if (NewlySpawned >= MaxNewMarkersPerRefresh)
		{
			continue;
		}

		// Spawn a transient, non-colliding marker whose mesh renders only into Custom Depth.
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Marker = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Loc), SpawnParams);
		if (!Marker)
		{
			continue;
		}
		Marker->SetActorEnableCollision(false);

		UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(Marker);
		MeshComp->SetMobility(EComponentMobility::Movable);
		Marker->SetRootComponent(MeshComp);
		MeshComp->SetStaticMesh(MarkerMesh);
		MeshComp->SetRelativeScale3D(FVector(0.5f));
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCastShadow(false);
		MeshComp->SetRenderInMainPass(false);        // invisible in normal rendering...
		MeshComp->SetRenderCustomDepth(true);        // ...visible only as an outline
		MeshComp->SetCustomDepthStencilValue(StencilFor(Category));
		MeshComp->RegisterComponent();

		RemoteMarkers.Add(Key, FTrackedMarker(Marker, Loc, Category));
		++NewlySpawned;
	}

	// Drop markers no longer wanted (collected, out of range, or now streamed in and outlined).
	for (auto It = RemoteMarkers.CreateIterator(); It; ++It)
	{
		AActor* Marker = It.Value().Actor.Get();
		if (!Wanted.Contains(It.Key()) || !IsValid(Marker))
		{
			if (IsValid(Marker))
			{
				Marker->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

void UVTCOutlineSubsystem::ClearRemoteMarkers()
{
	for (const TPair<uint64, FTrackedMarker>& Pair : RemoteMarkers)
	{
		if (AActor* Marker = Pair.Value.Actor.Get())
		{
			Marker->Destroy();
		}
	}
	RemoteMarkers.Reset();
}

// ---------------------------------------------------------------------------------------------

// Icon texture for the map dot. Imported per the Icon Library docs' guidance for in-game
// icon textures (Mip Gen Settings = FromTextureGroup, Texture Group = UI, Compression = UserInterface2D).
static const TCHAR* DotTexturePath = TEXT("/ViewThroughCollectibles/Textures/T_VTCDot.T_VTCDot");

UTexture2D* UVTCOutlineSubsystem::EnsureDotTexture()
{
	if (DotTexture)
	{
		return DotTexture;
	}

	DotTexture = LoadObject<UTexture2D>(nullptr, DotTexturePath);
	if (!bDotTextureLoadLogged)
	{
		bDotTextureLoadLogged = true;
		UE_LOG(LogViewThroughCollectibles, Display, TEXT("EnsureDotTexture: LoadObject(%s) -> %s"),
			DotTexturePath, DotTexture ? TEXT("OK") : TEXT("MISSING (import T_VTCDot.png there)"));
	}
	return DotTexture;
}

void UVTCOutlineSubsystem::RefreshMapDots(const FVTCConfigStruct& Cfg,
	const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines)
{
	UWorld* World = GetWorld();
	if (!World || !Cfg.ShowOnMap)
	{
		ClearMapDots();
		return;
	}

	UTexture2D* Icon = EnsureDotTexture();
	if (!Icon)
	{
		ClearMapDots();
		return;
	}

	AFGActorRepresentationManager* Mgr = AFGActorRepresentationManager::Get(World);
	if (!Mgr)
	{
		// Manager isn't replicated to the client yet (first ~20 s after spawn). Try next tick.
		return;
	}

	// Same reasoning as RefreshRemoteMarkers's spawn cap: creating a representation is real
	// work (manager bookkeeping, an immediate UpdateRepresentation call), so a fresh join or a
	// big jump in Max Distance mustn't create hundreds of them in one frame.
	constexpr int32 MaxNewMapDotsPerRefresh = 20;
	int32 Created = 0;
	TSet<uint64> Wanted;
	auto Want = [&](const FVector& Loc, EVTCCollectibleCategory Category)
	{
		const uint64 Key = MarkerKey(Loc, static_cast<uint8>(Category));
		if (Wanted.Contains(Key))
		{
			return;
		}
		Wanted.Add(Key);
		if (MapDots.Contains(Key) || Created >= MaxNewMapDotsPerRefresh)
		{
			return;
		}
		FLinearColor Colour = Cfg.GetFor(Category).GetLinearColor();
		Colour.A = 1.f;

		// Client-local map + compass representation. The actor-interface path
		// (CreateAndAddNewRepresentation) can't work here: Satisfactory only copies interface
		// visuals into the representation on the server (UpdateActorRepresentationFromInterface
		// is server-only), so a client marker keeps engine defaults. This convenience call sets
		// texture/colour/type directly and builds the compass icon from the texture.
		UFGActorRepresentation* Rep = Mgr->CreateAndAddNewRepresentationNoActor(
			Loc, Icon, Colour, /*lifeSpan*/ 0.f,
			/*shouldShowInCompass*/ true, /*shouldShowOnMap*/ true,
			ERepresentationType::RT_MapMarker, UVTCMapRepresentation::StaticClass());
		if (Rep)
		{
			if (UVTCMapRepresentation* MapRep = Cast<UVTCMapRepresentation>(Rep))
			{
				MapRep->SetMarkerText(CategoryLabel(Category));
				Mgr->UpdateRepresentation(MapRep);   // re-cache the label set after creation
			}
			MapDots.Add(Key, Rep);
			++Created;
			if (!bMapDotDiagLogged)
			{
				bMapDotDiagLogged = true;
				UMaterialInterface* CompassMat = Rep->GetRepresentationCompassMaterial();
				UE_LOG(LogViewThroughCollectibles, Display,
					TEXT("map dot rep created: type=RT_MapMarker label='%s' compassMaterial=%s texture=%s colour=%s"),
					*CategoryLabel(Category).ToString(),
					CompassMat ? *CompassMat->GetName() : TEXT("NULL"),
					Icon ? *Icon->GetName() : TEXT("null"), *Colour.ToString());
			}
		}
	};

	for (const TPair<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& Pair : LoadedOutlines)
	{
		if (const AActor* A = Pair.Key.Get())
		{
			Want(A->GetActorLocation(), Pair.Value);
		}
	}
	for (const TPair<uint64, FTrackedMarker>& Pair : RemoteMarkers)
	{
		Want(Pair.Value.Location, Pair.Value.Category);
	}

	for (auto It = MapDots.CreateIterator(); It; ++It)
	{
		UFGActorRepresentation* Rep = It.Value().Get();
		if (!Wanted.Contains(It.Key()) || !Rep)
		{
			if (Rep)
			{
				Mgr->RemoveRepresentation(Rep);
			}
			It.RemoveCurrent();
		}
	}

	UE_LOG(LogViewThroughCollectibles, Display,
		TEXT("RefreshMapDots: wanted %d keys, %d live map reps (+%d created this pass) from %d outlines + %d remote markers"),
		Wanted.Num(), MapDots.Num(), Created, LoadedOutlines.Num(), RemoteMarkers.Num());
}

void UVTCOutlineSubsystem::ClearMapDots()
{
	if (AFGActorRepresentationManager* Mgr = AFGActorRepresentationManager::Get(GetWorld()))
	{
		for (const TPair<uint64, TWeakObjectPtr<UFGActorRepresentation>>& Pair : MapDots)
		{
			if (UFGActorRepresentation* Rep = Pair.Value.Get())
			{
				Mgr->RemoveRepresentation(Rep);
			}
		}
	}
	MapDots.Reset();
}
