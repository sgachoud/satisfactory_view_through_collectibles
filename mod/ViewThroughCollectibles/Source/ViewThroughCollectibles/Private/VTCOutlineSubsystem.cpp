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
#include "TextureResource.h"
#include "PixelFormat.h"
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
	// AFGScannableSubsystem is a server-side AFGSubsystem and its pickup/drop-pod arrays are
	// Transient (not replicated) — a remote client gets empty lists from it. So we take
	// whatever it has, then fall back to a direct actor sweep whenever it gave us nothing
	// (i.e. on any real client, or before its cooked data is assigned).
	int32 ScannableHits = 0;
	if (AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(this))
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
	RefreshRemoteMarkers(Cfg, PlayerLoc, Desired, RegistryNearby, /*bHaveRegistry*/ ScannableHits > 0);

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

		RemoteMarkers.Add(Key, FTrackedMarker(Marker, Loc, Category));
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

UTexture2D* UVTCOutlineSubsystem::EnsureDotTexture()
{
	if (DotTexture)
	{
		return DotTexture;
	}

	// A small white filled circle; the representation tints it per category.
	constexpr int32 Size = 16;
	constexpr float R = Size * 0.5f;
	UTexture2D* Tex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;

	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float D = static_cast<float>(FMath::Sqrt(FMath::Square(X + 0.5f - R) + FMath::Square(Y + 0.5f - R)));
			const uint8 A = static_cast<uint8>(FMath::Clamp(255.f * (R - D), 0.f, 255.f));
			uint8* Px = Data + (Y * Size + X) * 4;
			Px[0] = Px[1] = Px[2] = 255;   // BGR white
			Px[3] = A;                     // circular alpha
		}
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();

	DotTexture = Tex;
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

	AFGActorRepresentationManager* RepMgr = AFGActorRepresentationManager::Get(World);
	UTexture2D* Icon = EnsureDotTexture();
	if (!RepMgr || !Icon)
	{
		ClearMapDots();
		return;
	}

	TSet<uint64> Wanted;
	auto Want = [&](const FVector& Loc, EVTCCollectibleCategory Category)
	{
		const uint64 Key = MarkerKey(Loc, static_cast<uint8>(Category));
		if (Wanted.Contains(Key))
		{
			return;
		}
		Wanted.Add(Key);
		if (MapDots.Contains(Key))
		{
			return;
		}
		FLinearColor Colour = Cfg.GetFor(Category).GetLinearColor();
		Colour.A = 1.f;
		UFGActorRepresentation* Rep = RepMgr->CreateAndAddNewRepresentationNoActor(
			Loc, Icon, Colour, /*lifeSpan*/ 0.f, /*compass*/ true, /*map*/ true,
			ERepresentationType::RT_Default, nullptr);
		if (Rep)
		{
			MapDots.Add(Key, Rep);
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
				RepMgr->RemoveRepresentation(Rep);
			}
			It.RemoveCurrent();
		}
	}
}

void UVTCOutlineSubsystem::ClearMapDots()
{
	if (AFGActorRepresentationManager* RepMgr =
		GetWorld() ? AFGActorRepresentationManager::Get(GetWorld()) : nullptr)
	{
		for (const TPair<uint64, TWeakObjectPtr<UFGActorRepresentation>>& Pair : MapDots)
		{
			if (UFGActorRepresentation* Rep = Pair.Value.Get())
			{
				RepMgr->RemoveRepresentation(Rep);
			}
		}
	}
	MapDots.Reset();
}
