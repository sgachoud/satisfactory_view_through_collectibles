#include "VTCOutlineSubsystem.h"
#include "VTCCollectibleFeedComponent.h"
#include "VTCDiscovery.h"
#include "VTCMapRepresentation.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "FGActorRepresentationManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogViewThroughCollectibles, Log, All);

namespace
{
	// A console variable is process-global, while world subsystems can overlap during travel.
	int32 JitterUsers = 0;
	const FName JitterTag(TEXT("ViewThroughCollectibles"));

	APlayerController* LocalController(UWorld* World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get(); PC && PC->IsLocalController()) return PC;
		return nullptr;
	}

	FText CategoryLabel(uint8 Category)
	{
		// Enum display metadata can be stripped from cooked builds.
		switch (static_cast<EVTCCollectibleCategory>(Category))
		{
		case EVTCCollectibleCategory::HardDrivePod: return NSLOCTEXT("VTC", "Pods", "Hard Drive Drop Pods");
		case EVTCCollectibleCategory::PowerSlugMk1: return NSLOCTEXT("VTC", "Slug1", "Power Slugs (Blue)");
		case EVTCCollectibleCategory::PowerSlugMk2: return NSLOCTEXT("VTC", "Slug2", "Power Slugs (Yellow)");
		case EVTCCollectibleCategory::PowerSlugMk3: return NSLOCTEXT("VTC", "Slug3", "Power Slugs (Purple)");
		case EVTCCollectibleCategory::MercerSphere: return NSLOCTEXT("VTC", "Spheres", "Mercer Spheres");
		case EVTCCollectibleCategory::Somersloop: return NSLOCTEXT("VTC", "Sloops", "Somersloops");
		case EVTCCollectibleCategory::BerylNut: return NSLOCTEXT("VTC", "Nuts", "Beryl Nut");
		case EVTCCollectibleCategory::Paleberry: return NSLOCTEXT("VTC", "Berries", "Paleberry");
		case EVTCCollectibleCategory::BaconAgaric: return NSLOCTEXT("VTC", "Agaric", "Bacon Agaric");
		default: return NSLOCTEXT("VTC", "Drops", "Dropped Items");
		}
	}
}

bool UVTCOutlineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && !IsRunningDedicatedServer();
}

void UVTCOutlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tables.Load();
}

void UVTCOutlineSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	ClearAllTrackedOutlines();
	ClearRemoteMarkers();
	ClearMapDots();
	RestoreCustomDepthJitterOverride();
	if (IsValid(PostProcessVolume)) PostProcessVolume->Destroy();
	PostProcessVolume = nullptr;
	OutlineMID = nullptr;
	Super::Deinitialize();
}

void UVTCOutlineSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.GetNetMode() == NM_DedicatedServer) return;
	SetupPostProcess();
	if (PostProcessVolume) ApplyCustomDepthJitterOverride();
	ArmedRefreshInterval = FVTCConfigStruct::GetActiveConfig(this).SafeRefreshIntervalSeconds();
	World.GetTimerManager().SetTimer(RefreshTimerHandle, this,
		&UVTCOutlineSubsystem::RefreshOutlines, ArmedRefreshInterval, true);
	RefreshOutlines();
}

void UVTCOutlineSubsystem::SetupPostProcess()
{
	ColorCollection = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/ViewThroughCollectibles/Materials/MPC_VTCColors.MPC_VTCColors"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/ViewThroughCollectibles/Materials/M_VTCOutline.M_VTCOutline"));
	if (!Material || !ColorCollection)
	{
		UE_LOG(LogViewThroughCollectibles, Error, TEXT("Outline assets missing; world outlines disabled."));
		return;
	}
	OutlineMID = UMaterialInstanceDynamic::Create(Material, this);
	if (!OutlineMID) return;
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PostProcessVolume = GetWorld()->SpawnActor<APostProcessVolume>(Params);
	if (!PostProcessVolume) return;
	PostProcessVolume->bUnbound = true;
	PostProcessVolume->BlendWeight = 1.f;
	PostProcessVolume->Priority = 1000000.f;
	PostProcessVolume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, OutlineMID));
}

void UVTCOutlineSubsystem::LogConfigIfChanged(const FVTCConfigStruct& Cfg)
{
	FString Digest = FString::Printf(TEXT("distance=%g refresh=%g max=%d thickness=%g fill=%g map=%d"),
		Cfg.MaxDistanceMeters, Cfg.RefreshIntervalSeconds, Cfg.MaxSimultaneousOutlines,
		Cfg.SafeThickness(), Cfg.SafeFillOpacity(), Cfg.ShowOnMap);
	for (uint8 I = 0; I < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++I)
	{
		const auto& Type = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(I));
		Digest += FString::Printf(TEXT(" %d:%d/%s"), I, Type.Enabled, *Type.Color);
	}
	if (Digest != LastLoggedConfigDigest)
	{
		LastLoggedConfigDigest = Digest;
		UE_LOG(LogViewThroughCollectibles, Verbose, TEXT("Config: %s"), *Digest);
	}
}

void UVTCOutlineSubsystem::PushConfigToMaterial(const FVTCConfigStruct& Cfg)
{
	if (!ColorCollection) return;
	bool Changed = !bConfigEverPushed || Cfg.SafeThickness() != LastPushedConfig.SafeThickness() ||
		Cfg.SafeFillOpacity() != LastPushedConfig.SafeFillOpacity();
	for (uint8 I = 0; !Changed && I < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++I)
	{
		const auto& A = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(I));
		const auto& B = LastPushedConfig.GetFor(static_cast<EVTCCollectibleCategory>(I));
		Changed = A.Enabled != B.Enabled || A.Color != B.Color;
	}
	if (!Changed) return;
	for (uint8 I = 0; I < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++I)
	{
		const auto& Type = Cfg.GetFor(static_cast<EVTCCollectibleCategory>(I));
		FLinearColor Color = Type.GetLinearColor();
		if (!Type.Enabled) Color.A = 0.f;
		UKismetMaterialLibrary::SetVectorParameterValue(this, ColorCollection,
			FName(*FString::Printf(TEXT("Color%d"), StencilFor(I))), Color);
	}
	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OutlineThicknessPixels"), Cfg.SafeThickness());
	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OccludedFillOpacity"), Cfg.SafeFillOpacity());
	LastPushedConfig = Cfg;
	bConfigEverPushed = true;
}

void UVTCOutlineSubsystem::ApplyCustomDepthJitterOverride()
{
	if (bOwnsJitterOverride) return;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")))
	{
		if (JitterUsers++ == 0)
		{
			CVar->Set(0, ECVF_SetByPluginHighPriority, JitterTag);
		}
		bOwnsJitterOverride = true;
	}
}

void UVTCOutlineSubsystem::RestoreCustomDepthJitterOverride()
{
	if (!bOwnsJitterOverride) return;
	bOwnsJitterOverride = false;
	if (--JitterUsers != 0) return;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")))
	{
		// Remove only our tagged history entry; other plugins and user overrides survive.
		CVar->Unset(ECVF_SetByPluginHighPriority, JitterTag);
	}
}

void UVTCOutlineSubsystem::RefreshOutlines()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	LogConfigIfChanged(Cfg);
	PushConfigToMaterial(Cfg);
	if (Cfg.SafeRefreshIntervalSeconds() != ArmedRefreshInterval)
	{
		ArmedRefreshInterval = Cfg.SafeRefreshIntervalSeconds();
		World->GetTimerManager().SetTimer(RefreshTimerHandle, this,
			&UVTCOutlineSubsystem::RefreshOutlines, ArmedRefreshInterval, true);
	}

	APlayerController* PC = LocalController(World);
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FVTCFeedPreferences Preferences = Cfg.FeedPreferences();
	auto* Feed = PC ? PC->FindComponentByClass<UVTCCollectibleFeedComponent>() : nullptr;
	if (Feed && (ReportedFeed.Get() != Feed || !(LastReportedPreferences == Preferences)))
	{
		ReportedFeed = Feed;
		LastReportedPreferences = Preferences;
		if (++RequestRevision == 0) ++RequestRevision;
		Feed->RequestPreferences(Preferences, RequestRevision);
	}
	if (!Feed) ReportedFeed.Reset();

	if (!Pawn)
	{
		ClearAllTrackedOutlines();
		ClearRemoteMarkers();
		ClearMapDots();
		return;
	}
	TMap<FGuid, FVTCCandidate> Candidates;
	if (World->GetNetMode() != NM_Client)
	{
		VTC::GatherRegistry(World, Tables, Candidates);
	}
	else if (Feed && Feed->GetSnapshot().Revision == RequestRevision)
	{
		for (const auto& Entry : Feed->GetSnapshot().Entries)
		{
			if (Entry.Id.IsValid() && Preferences.Includes(Entry.Category))
			{
				FVTCCandidate Candidate;
				Candidate.Data = Entry;
				Candidates.Add(Entry.Id, Candidate);
			}
		}
	}
	// A modded server's selection stays authoritative, including an empty response and
	// the wait for its first response. Loaded actors may only enrich its level-placed IDs.
	// Observed collection suppresses old feed records even after the actor streams out.
	VTC::GatherLoaded(World, Tables, RuntimeIds, Candidates, CollectionState,
		World->GetNetMode() == NM_Client && Feed != nullptr);
	const auto Selected = VTC::SelectCollectibles(Candidates, Pawn->GetActorLocation(), Preferences);

	TSet<TWeakObjectPtr<UMeshComponent>> WantedMeshes;
	TArray<FVTCCandidate> Markers;
	if (IsValid(PostProcessVolume))
	{
		for (const auto& Candidate : Selected)
		{
			if (!ApplyCustomDepth(Candidate.Actor.Get(), StencilFor(Candidate.Data.Category), WantedMeshes))
				Markers.Add(Candidate);
		}
	}
	ReleaseUnusedMeshes(WantedMeshes);
	RefreshRemoteMarkers(Markers);
	RefreshMapDots(Cfg, Selected);
}

bool UVTCOutlineSubsystem::FMeshState::StillOwned(const UMeshComponent* Mesh) const
{
	return Mesh->bRenderCustomDepth && Mesh->CustomDepthStencilValue == AppliedStencil &&
		Mesh->CustomDepthStencilWriteMask == ERendererStencilMask::ERSM_Default;
}

void UVTCOutlineSubsystem::FMeshState::Restore(UMeshComponent* Mesh) const
{
	if (!StillOwned(Mesh)) return;
	Mesh->SetCustomDepthStencilValue(OriginalStencil);
	Mesh->SetCustomDepthStencilWriteMask(static_cast<ERendererStencilMask>(OriginalWriteMask));
	Mesh->SetRenderCustomDepth(OriginalEnabled);
}

bool UVTCOutlineSubsystem::ApplyCustomDepth(AActor* Actor, int32 Stencil,
	TSet<TWeakObjectPtr<UMeshComponent>>& Wanted)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->IsHidden()) return false;
	bool Applied = false;
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame) continue;
		if (const auto* Static = Cast<UStaticMeshComponent>(Mesh); Static && !Static->GetStaticMesh()) continue;
		if (const auto* Skeletal = Cast<USkeletalMeshComponent>(Mesh); Skeletal && !Skeletal->GetSkeletalMeshAsset()) continue;
		FMeshState* State = MeshStates.Find(Mesh);
		if (State && !State->StillOwned(Mesh))
		{
			// Another system took over: relinquish this component rather than fighting it.
			MeshStates.Remove(Mesh);
			continue;
		}
		if (!State)
		{
			if (Mesh->bRenderCustomDepth) continue;
			FMeshState Initial;
			Initial.OriginalEnabled = Mesh->bRenderCustomDepth;
			Initial.OriginalStencil = Mesh->CustomDepthStencilValue;
			Initial.OriginalWriteMask = static_cast<uint8>(Mesh->CustomDepthStencilWriteMask);
			State = &MeshStates.Add(Mesh, Initial);
		}
		State->AppliedStencil = Stencil;
		Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
		Mesh->SetCustomDepthStencilValue(Stencil);
		Mesh->SetRenderCustomDepth(true);
		Wanted.Add(Mesh);
		Applied = true;
	}
	return Applied;
}

void UVTCOutlineSubsystem::ReleaseUnusedMeshes(const TSet<TWeakObjectPtr<UMeshComponent>>& Wanted)
{
	for (auto It = MeshStates.CreateIterator(); It; ++It)
	{
		UMeshComponent* Mesh = It.Key().Get();
		if (!Mesh || !Wanted.Contains(It.Key()))
		{
			if (Mesh) It.Value().Restore(Mesh);
			It.RemoveCurrent();
		}
	}
}

void UVTCOutlineSubsystem::ClearAllTrackedOutlines()
{
	ReleaseUnusedMeshes({});
}

void UVTCOutlineSubsystem::RefreshRemoteMarkers(const TArray<FVTCCandidate>& Selected)
{
	TSet<FGuid> Wanted;
	for (const auto& Candidate : Selected) Wanted.Add(Candidate.Data.Id);
	for (auto It = RemoteMarkers.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()) || !It.Value().IsValid())
		{
			if (AActor* Actor = It.Value().Get()) Actor->Destroy();
			It.RemoveCurrent();
		}
	}
	if (Selected.IsEmpty()) return;
	if (!bMarkerMeshAttempted)
	{
		bMarkerMeshAttempted = true;
		MarkerMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (!MarkerMesh) UE_LOG(LogViewThroughCollectibles, Error, TEXT("Sphere mesh missing; distant world markers disabled."));
	}
	if (!MarkerMesh) return;

	int32 Created = 0;
	for (const auto& Candidate : Selected)
	{
		AActor* Marker = RemoteMarkers.FindRef(Candidate.Data.Id).Get();
		if (!Marker)
		{
			if (Created >= 20) continue;
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Marker = GetWorld()->SpawnActor<AActor>(Params);
			if (!Marker) continue;
			Marker->SetActorEnableCollision(false);
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Marker);
			Mesh->SetMobility(EComponentMobility::Movable);
			Marker->SetRootComponent(Mesh);
			Mesh->SetStaticMesh(MarkerMesh);
			Mesh->SetRelativeScale3D(FVector(0.5));
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->SetRenderInMainPass(false);
			Mesh->SetRenderInDepthPass(false);
			Mesh->SetRenderCustomDepth(true);
			Mesh->RegisterComponent();
			RemoteMarkers.Add(Candidate.Data.Id, Marker);
			++Created;
		}
		// The root must exist before assigning a transform to a plain AActor.
		Marker->SetActorLocation(Candidate.Data.Location);
		CastChecked<UStaticMeshComponent>(Marker->GetRootComponent())->SetCustomDepthStencilValue(StencilFor(Candidate.Data.Category));
	}
}

void UVTCOutlineSubsystem::ClearRemoteMarkers()
{
	for (const auto& Pair : RemoteMarkers)
		if (AActor* Marker = Pair.Value.Get()) Marker->Destroy();
	RemoteMarkers.Reset();
}

void UVTCOutlineSubsystem::RefreshMapDots(const FVTCConfigStruct& Cfg, const TArray<FVTCCandidate>& Selected)
{
	if (!Cfg.ShowOnMap)
	{
		ClearMapDots();
		return;
	}
	auto* Manager = AFGActorRepresentationManager::Get(GetWorld());
	if (!Manager) return;
	if (!bDotTextureAttempted)
	{
		bDotTextureAttempted = true;
		DotTexture = LoadObject<UTexture2D>(nullptr, TEXT("/ViewThroughCollectibles/Textures/T_VTCDot.T_VTCDot"));
		if (!DotTexture) UE_LOG(LogViewThroughCollectibles, Error, TEXT("Map dot texture missing; map dots disabled."));
	}
	if (!DotTexture) return;

	TSet<FGuid> Wanted;
	for (const auto& Candidate : Selected) Wanted.Add(Candidate.Data.Id);
	for (auto It = MapDots.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()) || !It.Value().IsValid())
		{
			if (auto* Rep = It.Value().Get())
			{
				Rep->SetHighlighted(false);
				Manager->RemoveRepresentation(Rep);
			}
			It.RemoveCurrent();
		}
	}
	int32 Created = 0;
	for (const auto& Candidate : Selected)
	{
		const auto Category = static_cast<EVTCCollectibleCategory>(Candidate.Data.Category);
		FLinearColor Color = Cfg.GetFor(Category).GetLinearColor();
		Color.A = 1.f;
		auto* Rep = MapDots.FindRef(Candidate.Data.Id).Get();
		if (!Rep)
		{
			if (Created >= 20) continue;
			Rep = Cast<UVTCMapRepresentation>(Manager->CreateNewRepresentationNoActor(
				Candidate.Data.Location, DotTexture, Color, 0.f, false, true,
				ERepresentationType::RT_MapMarker, UVTCMapRepresentation::StaticClass()));
			if (!Rep) continue;
			Rep->UpdateMarker(Candidate.Data.Id, Candidate.Data.Location, Color, CategoryLabel(Candidate.Data.Category));
			MapDots.Add(Candidate.Data.Id, Rep);
			KnownMapMarkerIds.Add(Candidate.Data.Id);
			// Publish only after the popup can resolve the collectible's marker GUID.
			Manager->AddRepresentation(Rep);
			++Created;
		}
		if (Rep->UpdateMarker(Candidate.Data.Id, Candidate.Data.Location, Color, CategoryLabel(Candidate.Data.Category)))
			Manager->UpdateRepresentation(Rep);
	}
}

void UVTCOutlineSubsystem::ClearMapDots()
{
	auto* Manager = AFGActorRepresentationManager::Get(GetWorld());
	for (const auto& Pair : MapDots)
	{
		if (auto* Rep = Pair.Value.Get())
		{
			Rep->SetHighlighted(false);
			if (Manager) Manager->RemoveRepresentation(Rep);
		}
	}
	MapDots.Reset();
}
