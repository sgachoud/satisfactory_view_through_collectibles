#include "VTCOutlineSubsystem.h"

#include "VTCConfig.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// FactoryGame.
// TODO(needs FactoryGame source access): confirm these header names and the class/API names
// used below against the engine version you build against. They match public SML headers as
// of Satisfactory 1.x but are the most likely thing to drift.
#include "FGOutlineComponent.h"
#include "FGCharacterPlayer.h"
#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "FGDropPod.h"

DEFINE_LOG_CATEGORY_STATIC(LogViewThroughCollectibles, Log, All);

namespace
{
	FORCEINLINE double DistSq(const AActor* A, const FVector& P)
	{
		return FVector::DistSquared(A->GetActorLocation(), P);
	}
}

bool UVTCOutlineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Cosmetic client-side effect: pointless (and unwanted) on a dedicated server.
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	// Only real game / PIE worlds, not editor preview or inactive worlds.
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return true;
}

void UVTCOutlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadClassCategoryTable();
}

void UVTCOutlineSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	HideAllTrackedOutlines();
	Super::Deinitialize();
}

void UVTCOutlineSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	const float Interval = FMath::Clamp(Cfg.RefreshIntervalSeconds, 0.1f, 5.f);

	InWorld.GetTimerManager().SetTimer(
		RefreshTimerHandle, this, &UVTCOutlineSubsystem::RefreshOutlines, Interval, /*loop*/ true);

	RebuildCandidateCache();
	RefreshOutlines();
}

UFGOutlineComponent* UVTCOutlineSubsystem::GetLocalOutlineComponent() const
{
	// UFGOutlineComponent exposes a world-context helper that returns the local player's
	// outline component; it is the way the base game and other mods reach it.
	// TODO(needs FactoryGame source access): confirm this static helper's name/signature.
	if (UFGOutlineComponent* Outline = UFGOutlineComponent::GetOutlineComponent(this))
	{
		return Outline;
	}

	// Fallback: dig it out of the local player pawn.
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(PC->GetPawn()))
			{
				return Character->FindComponentByClass<UFGOutlineComponent>();
			}
		}
	}
	return nullptr;
}

void UVTCOutlineSubsystem::RebuildCandidateCache()
{
	CandidateActors.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// AFGItemPickup covers power slugs, Mercer Spheres, Somersloops, collectible flora, and
	// (via AFGItemPickup_Spawnable) items dropped on the ground.
	for (TActorIterator<AFGItemPickup> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			CandidateActors.Add(*It);
		}
	}

	// Hard drives live inside their crash-site drop pod until looted.
	for (TActorIterator<AFGDropPod> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			CandidateActors.Add(*It);
		}
	}

	LastCandidateRebuildSeconds = World->GetTimeSeconds();
}

bool UVTCOutlineSubsystem::ResolveCategory(const AActor* Actor, EVTCCollectibleCategory& OutCategory) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	// Dropped items: identified by native type, no class-path lookup needed.
	if (Actor->IsA(AFGItemPickup_Spawnable::StaticClass()))
	{
		OutCategory = EVTCCollectibleCategory::DroppedItem;
		return true;
	}

	// Everything else: walk the class hierarchy against the (ini-overridable) class table.
	for (const UClass* C = Actor->GetClass(); C && C != AActor::StaticClass(); C = C->GetSuperClass())
	{
		const FSoftClassPath Path{ C };
		if (const EVTCCollectibleCategory* Found = CategoryByClassPath.Find(Path))
		{
			OutCategory = *Found;
			return true;
		}
	}
	return false;
}

void UVTCOutlineSubsystem::RefreshOutlines()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UFGOutlineComponent* Outline = GetLocalOutlineComponent();
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	if (!Outline || !LocalPawn)
	{
		// Player dead / still loading. Hide what we can and forget the rest so the next
		// successful pass re-shows from a clean slate.
		HideAllTrackedOutlines();
		return;
	}

	if (World->GetTimeSeconds() - LastCandidateRebuildSeconds >= CandidateRebuildIntervalSeconds)
	{
		RebuildCandidateCache();
	}

	const FVTCConfigStruct Cfg = FVTCConfigStruct::GetActiveConfig(this);
	const FVector PlayerLoc = LocalPawn->GetActorLocation();
	const double MaxDistSq = FMath::Square(static_cast<double>(FMath::Max(10.f, Cfg.MaxDistanceMeters)) * 100.0);
	const int32 MaxOutlines = FMath::Clamp(Cfg.MaxSimultaneousOutlines, 1, 1024);

	// Gather everything in range, with its distance, so we can keep only the nearest N.
	struct FDesired
	{
		AActor* Actor;
		EOutlineColor Color;
		double DistSq;
	};
	TArray<FDesired> InRange;

	for (const TWeakObjectPtr<AActor>& WeakActor : CandidateActors)
	{
		AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		EVTCCollectibleCategory Category;
		if (!ResolveCategory(Actor, Category))
		{
			continue;
		}
		if (Category == EVTCCollectibleCategory::HardDrivePod && bHardDrivePodOutlineDisabled)
		{
			continue;
		}

		const FVTCTypeSettings& T = Cfg.GetFor(Category);
		if (!T.bEnabled)
		{
			continue;
		}

		const double D = DistSq(Actor, PlayerLoc);
		if (D <= MaxDistSq)
		{
			InRange.Add({ Actor, T.Color, D });
		}
	}

	if (InRange.Num() > MaxOutlines)
	{
		InRange.Sort([](const FDesired& A, const FDesired& B) { return A.DistSq < B.DistSq; });
		InRange.SetNum(MaxOutlines);
	}

	TMap<TWeakObjectPtr<AActor>, EOutlineColor> Desired;
	Desired.Reserve(InRange.Num());
	for (const FDesired& D : InRange)
	{
		Desired.Add(D.Actor, D.Color);
	}

	// Hide anything no longer wanted (or whose colour changed).
	for (auto It = TrackedOutlines.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		const EOutlineColor* NewColor = IsValid(Actor) ? Desired.Find(Actor) : nullptr;
		if (!NewColor || *NewColor != It.Value())
		{
			if (IsValid(Actor))
			{
				Outline->HideOutline(Actor);
			}
			It.RemoveCurrent();
		}
	}

	// Show anything newly wanted.
	for (const TPair<TWeakObjectPtr<AActor>, EOutlineColor>& Pair : Desired)
	{
		AActor* Actor = Pair.Key.Get();
		if (!IsValid(Actor) || TrackedOutlines.Contains(Actor))
		{
			continue;
		}

		// Known risk: UFGOutlineComponent::ShowOutline has been reported to ensure/crash on
		// drop pods ("copy Custom Primitive Data from invalid index", e.g. with Long Reach).
		// TODO(needs FactoryGame source access): before calling ShowOutline on a drop pod,
		// verify the pod's outlined mesh component has valid custom primitive data, and set
		// bHardDrivePodOutlineDisabled on failure so we stop retrying that category.
		Outline->ShowOutline(Actor, Pair.Value);
		TrackedOutlines.Add(Actor, Pair.Value);
	}
}

void UVTCOutlineSubsystem::HideAllTrackedOutlines()
{
	if (UFGOutlineComponent* Outline = GetLocalOutlineComponent())
	{
		for (const TPair<TWeakObjectPtr<AActor>, EOutlineColor>& Pair : TrackedOutlines)
		{
			if (AActor* Actor = Pair.Key.Get())
			{
				Outline->HideOutline(Actor);
			}
		}
	}
	TrackedOutlines.Reset();
}

void UVTCOutlineSubsystem::LoadClassCategoryTable()
{
	CategoryByClassPath.Reset();

	// Built-in defaults. Paths are best-effort and MUST be verified against a real install
	// (FModel / in-editor) -- see docs/collectible-classes.md. A wrong path here only makes
	// that one category inert; fix it in-place or via the ini override below, no recompile.
	auto Add = [this](const TCHAR* Path, EVTCCollectibleCategory Cat)
	{
		CategoryByClassPath.Add(FSoftClassPath(Path), Cat);
	};

	Add(TEXT("/Game/FactoryGame/World/Benefit/DropPod/BP_DropPod.BP_DropPod_C"), EVTCCollectibleCategory::HardDrivePod);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal.BP_Crystal_C"), EVTCCollectibleCategory::PowerSlugMk1);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk2.BP_Crystal_mk2_C"), EVTCCollectibleCategory::PowerSlugMk2);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk3.BP_Crystal_mk3_C"), EVTCCollectibleCategory::PowerSlugMk3);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/AlienArtifacts/BP_WAT1.BP_WAT1_C"), EVTCCollectibleCategory::MercerSphere);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/AlienArtifacts/BP_WAT2.BP_WAT2_C"), EVTCCollectibleCategory::Somersloop);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Berry/BP_Berry.BP_Berry_C"), EVTCCollectibleCategory::BerylNut);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Nut/BP_Nut.BP_Nut_C"), EVTCCollectibleCategory::Paleberry);
	Add(TEXT("/Game/FactoryGame/Resource/Environment/Shroom/BP_Shroom.BP_Shroom_C"), EVTCCollectibleCategory::BaconAgaric);

	// Ini overrides: add/replace entries without recompiling. Put in a mod ini, e.g.
	// [/Script/ViewThroughCollectibles] section is not used; instead read a simple map from
	// the "ViewThroughCollectibles" config category of Game.ini:
	//   [ViewThroughCollectibles.ClassCategories]
	//   +Entry=(Path="/Game/.../BP_Foo.BP_Foo_C", Category="Somersloop")
	TArray<FString> Entries;
	GConfig->GetArray(TEXT("ViewThroughCollectibles.ClassCategories"), TEXT("Entry"), Entries, GGameIni);
	for (const FString& Entry : Entries)
	{
		FString PathStr, CategoryStr;
		if (FParse::Value(*Entry, TEXT("Path="), PathStr) && FParse::Value(*Entry, TEXT("Category="), CategoryStr))
		{
			const int64 EnumVal = StaticEnum<EVTCCollectibleCategory>()->GetValueByNameString(CategoryStr);
			if (EnumVal != INDEX_NONE)
			{
				CategoryByClassPath.Add(FSoftClassPath(PathStr), static_cast<EVTCCollectibleCategory>(EnumVal));
			}
		}
	}
}
