#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VTCTypes.h"
#include "VTCOutlineSubsystem.generated.h"

class AActor;
class UFGOutlineComponent;

/**
 * Client-side, cosmetic-only subsystem that keeps the game's outline component pointed at
 * the collectibles the local player has enabled and is currently near.
 *
 * Multiplayer: this never runs on a dedicated server, never spawns or replicates anything,
 * and only reads already-replicated actor transforms. Two clients can have completely
 * different settings; neither affects the other or the save.
 */
UCLASS()
class VIEWTHROUGHCOLLECTIBLES_API UVTCOutlineSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** Recompute the desired outline set and diff it against what is currently shown. */
	void RefreshOutlines();

	/** Rebuild CandidateActors by sweeping the world for collectible base classes. */
	void RebuildCandidateCache();

	/** Map an actor to a collectible category, or return false if it is not one we handle. */
	bool ResolveCategory(const AActor* Actor, EVTCCollectibleCategory& OutCategory) const;

	/** Resolve the local player's outline component, or nullptr if unavailable this frame. */
	UFGOutlineComponent* GetLocalOutlineComponent() const;

	void HideAllTrackedOutlines();

	/** Populate CategoryByClassPath from built-in defaults + [/Script/...] ini overrides. */
	void LoadClassCategoryTable();

	FTimerHandle RefreshTimerHandle;

	/** Slow-changing set of things worth distance-checking each refresh. */
	TArray<TWeakObjectPtr<AActor>> CandidateActors;
	double LastCandidateRebuildSeconds = 0.0;

	/** What is outlined right now, and in which colour, so we only touch changes. */
	TMap<TWeakObjectPtr<AActor>, EOutlineColor> TrackedOutlines;

	/** Class path -> category. Seeded in LoadClassCategoryTable(), ini-overridable. */
	TMap<FSoftClassPath, EVTCCollectibleCategory> CategoryByClassPath;

	/** Set when ShowOutline() throws/ensures on a drop pod, to stop retrying that category. */
	bool bHardDrivePodOutlineDisabled = false;

	static constexpr double CandidateRebuildIntervalSeconds = 2.0;
};
