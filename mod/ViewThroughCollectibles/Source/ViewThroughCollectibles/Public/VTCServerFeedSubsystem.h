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

	/** Hard cap on entries sent to one player, regardless of config distance. */
	static constexpr int32 MaxEntriesPerPlayer = 400;
};
