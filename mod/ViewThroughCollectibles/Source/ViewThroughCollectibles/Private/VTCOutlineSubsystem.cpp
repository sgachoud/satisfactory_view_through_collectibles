#include "VTCOutlineSubsystem.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "UObject/ReflectedTypeAccessors.h"

// FactoryGame. Header paths verified against SML 3.12 / FactoryGame CL 502094.
#include "FGScannableSubsystem.h"
#include "FGWorldScannableData.h"
#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "FGDropPod.h"
#include "Resources/FGItemDescriptor.h"

DEFINE_LOG_CATEGORY_STATIC(LogViewThroughCollectibles, Log, All);

const TCHAR* UVTCOutlineSubsystem::OutlineMaterialPath =
	TEXT("/ViewThroughCollectibles/Materials/M_VTCOutline.M_VTCOutline");
const TCHAR* UVTCOutlineSubsystem::ColorCollectionPath =
	TEXT("/ViewThroughCollectibles/Materials/MPC_VTCColors.MPC_VTCColors");

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
	LoadCategoryTables();
}

void UVTCOutlineSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	ClearAllTrackedOutlines();

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
			bChanged = A.bEnabled != B.bEnabled || !A.Color.Equals(B.Color);
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
		FLinearColor Value = T.Color;
		Value.A = T.bEnabled ? T.Color.A : 0.f; // alpha 0 => material draws nothing for this stencil
		UKismetMaterialLibrary::SetVectorParameterValue(this, ColorCollection, ParamName, Value);
	}

	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OutlineThicknessPixels"), Cfg.OutlineThicknessPixels);
	UKismetMaterialLibrary::SetScalarParameterValue(this, ColorCollection, TEXT("OccludedFillOpacity"), Cfg.OccludedFillOpacity);

	LastPushedConfig = Cfg;
	bConfigEverPushed = true;
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
		if (!ResolveCategory(Actor, FallbackClass, Category))
		{
			return;
		}
		if (!Cfg.GetFor(Category).bEnabled)
		{
			return;
		}
		Candidates.Add({ Actor, Category, D });
	};

	// --- Level-placed collectibles via the game's own scannable registry -------------------
	if (AFGScannableSubsystem* Scan = AFGScannableSubsystem::Get(this))
	{
		for (const FWorldScannableData& Data : Scan->GetAvailableItemPickups())
		{
			if (!Scan->DoesPickupExist(Data.ActorGuid))
			{
				continue; // already collected / destroyed
			}
			Consider(Data.Actor.Get(), Data.ActorClass.Get(), Data.ActorLocation);
		}
		for (const FWorldScannableData& Data : Scan->GetAvailableDropPods())
		{
			if (Scan->HasDropPodBeenLooted(Data.ActorGuid))
			{
				continue;
			}
			Consider(Data.Actor.Get(), Data.ActorClass.Get(), Data.ActorLocation);
		}
	}
	else
	{
		// Fallback: sweep actors directly (older saves / very early call).
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
}

// ---------------------------------------------------------------------------------------------

bool UVTCOutlineSubsystem::ResolveCategory(const AActor* Actor, const UClass* FallbackClass, EVTCCollectibleCategory& OutCategory) const
{
	if (Actor)
	{
		if (Actor->IsA(AFGDropPod::StaticClass()))
		{
			OutCategory = EVTCCollectibleCategory::HardDrivePod;
			return true;
		}
		if (Actor->IsA(AFGItemPickup_Spawnable::StaticClass()))
		{
			OutCategory = EVTCCollectibleCategory::DroppedItem;
			return true;
		}
		if (const AFGItemPickup* Pickup = Cast<AFGItemPickup>(Actor))
		{
			const TSubclassOf<UFGItemDescriptor> Desc = Pickup->GetPickupItemClass();
			if (*Desc)
			{
				if (const EVTCCollectibleCategory* Found = CategoryByDescriptor.Find(FSoftClassPath(*Desc)))
				{
					OutCategory = *Found;
					return true;
				}
			}
		}
	}

	// Class-path fallback (used when the actor is not streamed in, or descriptor lookup missed).
	for (const UClass* C = Actor ? Actor->GetClass() : FallbackClass;
	     C && C != AActor::StaticClass(); C = C->GetSuperClass())
	{
		if (const EVTCCollectibleCategory* Found = CategoryByActorClass.Find(FSoftClassPath(C)))
		{
			OutCategory = *Found;
			return true;
		}
	}
	return false;
}

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

void UVTCOutlineSubsystem::LoadCategoryTables()
{
	CategoryByDescriptor.Reset();
	CategoryByActorClass.Reset();

	auto Desc = [this](const TCHAR* Path, EVTCCollectibleCategory Cat)
	{
		CategoryByDescriptor.Add(FSoftClassPath(Path), Cat);
	};
	auto Cls = [this](const TCHAR* Path, EVTCCollectibleCategory Cat)
	{
		CategoryByActorClass.Add(FSoftClassPath(Path), Cat);
	};

	// Verified paths - see docs/FINDINGS.md.
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal.Desc_Crystal_C"), EVTCCollectibleCategory::PowerSlugMk1);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal_mk2.Desc_Crystal_mk2_C"), EVTCCollectibleCategory::PowerSlugMk2);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal_mk3.Desc_Crystal_mk3_C"), EVTCCollectibleCategory::PowerSlugMk3);
	Desc(TEXT("/Game/FactoryGame/Prototype/WAT/Desc_WAT1.Desc_WAT1_C"), EVTCCollectibleCategory::MercerSphere);
	Desc(TEXT("/Game/FactoryGame/Prototype/WAT/Desc_WAT2.Desc_WAT2_C"), EVTCCollectibleCategory::Somersloop);
	// Folder names don't match in-game names: Desc_Berry = Beryl Nut, Desc_Nut = Paleberry,
	// Desc_Shroom = Bacon Agaric. TODO verify in-game before shipping.
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Berry/Desc_Berry.Desc_Berry_C"), EVTCCollectibleCategory::BerylNut);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Nut/Desc_Nut.Desc_Nut_C"), EVTCCollectibleCategory::Paleberry);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/DesertShroom/Desc_Shroom.Desc_Shroom_C"), EVTCCollectibleCategory::BaconAgaric);

	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal.BP_Crystal_C"), EVTCCollectibleCategory::PowerSlugMk1);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk2.BP_Crystal_mk2_C"), EVTCCollectibleCategory::PowerSlugMk2);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk3.BP_Crystal_mk3_C"), EVTCCollectibleCategory::PowerSlugMk3);
	Cls(TEXT("/Game/FactoryGame/Prototype/WAT/BP_WAT1.BP_WAT1_C"), EVTCCollectibleCategory::MercerSphere);
	Cls(TEXT("/Game/FactoryGame/Prototype/WAT/BP_WAT2.BP_WAT2_C"), EVTCCollectibleCategory::Somersloop);

	// Ini overrides (Game.ini): add/replace without recompiling.
	//   [ViewThroughCollectibles.Categories]
	//   +Descriptor=(Path="/Game/.../Desc_Foo.Desc_Foo_C", Category="Somersloop")
	//   +ActorClass=(Path="/Game/.../BP_Foo.BP_Foo_C", Category="Somersloop")
	auto ApplyOverrides = [](const TCHAR* Key, TMap<FSoftClassPath, EVTCCollectibleCategory>& Table)
	{
		TArray<FString> Entries;
		GConfig->GetArray(TEXT("ViewThroughCollectibles.Categories"), Key, Entries, GGameIni);
		for (const FString& Entry : Entries)
		{
			FString PathStr, CategoryStr;
			if (FParse::Value(*Entry, TEXT("Path="), PathStr) && FParse::Value(*Entry, TEXT("Category="), CategoryStr))
			{
				const int64 Val = StaticEnum<EVTCCollectibleCategory>()->GetValueByNameString(CategoryStr);
				if (Val != INDEX_NONE)
				{
					Table.Add(FSoftClassPath(PathStr), static_cast<EVTCCollectibleCategory>(Val));
				}
			}
		}
	};
	ApplyOverrides(TEXT("Descriptor"), CategoryByDescriptor);
	ApplyOverrides(TEXT("ActorClass"), CategoryByActorClass);
}
