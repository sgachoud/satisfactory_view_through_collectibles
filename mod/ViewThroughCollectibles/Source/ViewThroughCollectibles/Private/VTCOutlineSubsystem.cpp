#include "VTCOutlineSubsystem.h"

#include "VTCCollectibleFeedComponent.h"

#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
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

	const float Interval = FMath::Clamp(Cfg.RefreshIntervalSeconds, 0.1f, 5.f);
	InWorld.GetTimerManager().SetTimer(
		RefreshTimerHandle, this, &UVTCOutlineSubsystem::RefreshOutlines, Interval, /*loop*/ true);

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
	PushConfigToMaterial(Cfg);

	const FVector PlayerLoc = LocalPawn->GetActorLocation();
	const double MaxDistSq = FMath::Square(static_cast<double>(FMath::Max(10.f, Cfg.MaxDistanceMeters)) * 100.0);
	const int32 MaxOutlines = FMath::Clamp(Cfg.MaxSimultaneousOutlines, 1, 2048);

	TArray<FCandidate> Candidates;

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
	// AFGScannableSubsystem is a server-side AFGSubsystem and its pickup/drop-pod arrays are
	// Transient (not replicated) — a remote client gets empty lists from it. So we take
	// whatever it has, then fall back to a direct actor sweep whenever it gave us nothing
	// (i.e. on any real client, or before its cooked data is assigned).
	int32 ScannableHits = 0;
	if (AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(this))
	{
		for (const FWorldScannableData& Data : Scan->GetAvailableItemPickups())
		{
			if (!Scan->DoesPickupExist(Data.ActorGuid))
			{
				continue; // already collected / destroyed
			}
			Consider(Data.Actor.Get(), Data.ActorClass.Get(), Data.ActorLocation);
			++ScannableHits;
		}
		for (const FWorldScannableData& Data : Scan->GetAvailableDropPods())
		{
			if (Scan->HasDropPodBeenLooted(Data.ActorGuid))
			{
				continue;
			}
			Consider(Data.Actor.Get(), Data.ActorClass.Get(), Data.ActorLocation);
			++ScannableHits;
		}
	}

	if (ScannableHits == 0)
	{
		// Sweep streamed-in actors directly. On a client this is the only source of
		// collectibles; nearby ones are replicated normally so their meshes exist here.
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
	for (auto It = TrackedOutlines.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		const EVTCCollectibleCategory* Now = IsValid(Actor) ? Desired.Find(Actor) : nullptr;
		if (!Now || *Now != It.Value())
		{
			if (IsValid(Actor))
			{
				ApplyCustomDepth(Actor, 0, false);
			}
			It.RemoveCurrent();
		}
	}
	for (const TPair<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& Pair : Desired)
	{
		AActor* Actor = Pair.Key.Get();
		if (IsValid(Actor) && !TrackedOutlines.Contains(Actor))
		{
			ApplyCustomDepth(Actor, StencilFor(Pair.Value), true);
			TrackedOutlines.Add(Actor, Pair.Value);
		}
	}

	// --- Distant collectibles the server told us about but we haven't streamed in ----------
	RefreshRemoteMarkers(Cfg, PlayerLoc, Desired);
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
}

void UVTCOutlineSubsystem::RefreshRemoteMarkers(const FVTCConfigStruct& Cfg, const FVector& PlayerLoc,
	const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float MarkerMeters = Cfg.RemoteMarkerMaxDistanceMeters;
	if (MarkerMeters <= 0.f)
	{
		ClearRemoteMarkers();
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

	// Candidate positions come from whichever source we have:
	//  - solo / listen-server host: the local AFGScannableSubsystem registry (has every
	//    collectible regardless of streaming),
	//  - remote client: the server-fed list on our PlayerController's feed component,
	//  - neither: no markers.
	TArray<FVTCFedCollectible> Candidates;
	if (AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(this))
	{
		auto Gather = [&](const TArray<FWorldScannableData>& Data, bool bDropPods)
		{
			for (const FWorldScannableData& D : Data)
			{
				if (bDropPods ? Scan->HasDropPodBeenLooted(D.ActorGuid) : !Scan->DoesPickupExist(D.ActorGuid))
				{
					continue;
				}
				EVTCCollectibleCategory Cat;
				if (Tables.Resolve(D.Actor.Get(), D.ActorClass.Get(), Cat))
				{
					FVTCFedCollectible F;
					F.Location = D.ActorLocation;
					F.Category = static_cast<uint8>(Cat);
					Candidates.Add(F);
				}
			}
		};
		Gather(Scan->GetAvailableItemPickups(), /*bDropPods*/ false);
		Gather(Scan->GetAvailableDropPods(), /*bDropPods*/ true);
	}
	if (Candidates.Num() == 0)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const UVTCCollectibleFeedComponent* Feed =
			PC ? PC->FindComponentByClass<UVTCCollectibleFeedComponent>() : nullptr;
		if (!Feed)
		{
			ClearRemoteMarkers();
			return;
		}
		Candidates = Feed->GetFeed();
	}

	const double MarkerDistSq = FMath::Square(static_cast<double>(MarkerMeters) * 100.0);
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
	for (const FVTCFedCollectible& Entry : Candidates)
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
	const int32 MaxMarkers = FMath::Clamp(Cfg.MaxSimultaneousOutlines, 1, 2048);
	if (WantedList.Num() > MaxMarkers)
	{
		WantedList.SetNum(MaxMarkers);
	}

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

		RemoteMarkers.Add(Key, Marker);
	}

	// Drop markers no longer wanted (collected, out of range, or now streamed in and outlined).
	for (auto It = RemoteMarkers.CreateIterator(); It; ++It)
	{
		AActor* Marker = It.Value().Get();
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
	for (const TPair<uint64, TWeakObjectPtr<AActor>>& Pair : RemoteMarkers)
	{
		if (AActor* Marker = Pair.Value.Get())
		{
			Marker->Destroy();
		}
	}
	RemoteMarkers.Reset();
}
