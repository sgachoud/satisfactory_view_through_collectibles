#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VTCCategoryTables.h"
#include "VTCServerFeedSubsystem.generated.h"

/**
 * Server-authority half of the mod. The collectible registry (AFGScannableSubsystem) is
 * server-side and unreplicated, so a remote client can only outline collectibles it has
 * streamed in. This subsystem runs on the listen/dedicated server, and for each connected
 * player pushes a distance-limited list of nearby collectibles down to that player's
 * UVTCCollectibleFeedComponent. The client turns entries it hasn't streamed in into
 * see-through markers.
 *
 * Purely additive: if the server doesn't have the mod this never runs, and clients fall
 * back to the streamed-in-actor sweep. Never touches the save, never spawns gameplay actors.
 */
UCLASS()
class UVTCServerFeedSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void RefreshFeeds();

	FVTCCategoryTables Tables;
	FTimerHandle RefreshTimerHandle;

	/** Baseline cap on entries sent to one player, to bound per-tick replication bandwidth.
	 *  Raised to match the player's own Max Simultaneous Outlines when that's set higher, so
	 *  this never silently overrides what they asked to see — only ever a bandwidth floor. */
	static constexpr int32 MaxEntriesPerPlayer = 400;

	/** Log the looted/collected filtering counts once, and again whenever they change, so a
	 *  stuck filter (collected items that never drop out) is visible in the server log. */
	int32 LastLoggedPickupTotal = -1;
	int32 LastLoggedPickupLive = -1;
	int32 LastLoggedPodTotal = -1;
	int32 LastLoggedPodLive = -1;

	/** Same idea, for the per-player feed itself: distance/cap used and how many were sent. */
	double LastLoggedFeedDistSq = -1.0;
	int32 LastLoggedCap = -1;
	int32 LastLoggedInRange = -1;
	int32 LastLoggedFeedSent = -1;
};
