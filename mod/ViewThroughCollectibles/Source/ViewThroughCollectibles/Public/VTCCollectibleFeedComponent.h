#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VTCTypes.h"
#include "VTCCollectibleFeedComponent.generated.h"

/**
 * Server <-> owning-client channel for nearby collectibles the client probably hasn't
 * streamed in. The server's UVTCServerFeedSubsystem adds one of these to each
 * APlayerController and refreshes its list; it replicates only to that player's client
 * (a PlayerController is relevant to its owner alone). The client's UVTCOutlineSubsystem
 * reads it to place see-through markers.
 *
 * The range that feed should cover is the CLIENT's own Max Distance / Max Simultaneous
 * setting, not the server's — each machine has its own copy of the mod's config, and the
 * server has no other way to know what a given player asked to see. So this also carries a
 * client -> server report of that preference, which UVTCServerFeedSubsystem reads when
 * building this player's feed (falling back to its own local config until the first report
 * arrives, e.g. right after connecting).
 *
 * If the server doesn't have the mod, this component never exists on the client and the
 * outline subsystem simply skips markers.
 */
UCLASS(ClassGroup = (Custom), NotBlueprintable)
class VIEWTHROUGHCOLLECTIBLES_API UVTCCollectibleFeedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVTCCollectibleFeedComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: replace the fed list (no-op if unchanged, to avoid needless replication). */
	void SetFeed(TArray<FVTCFedCollectible>&& NewFeed);

	/** Client: the current list. */
	const TArray<FVTCFedCollectible>& GetFeed() const { return Feed; }

	/** Client -> server: this player's own Max Distance (cm) / Max Simultaneous Outlines,
	 *  so their feed covers what THEY asked for rather than the server's local default. */
	UFUNCTION(Server, Reliable)
	void Server_ReportRangePreference(float InMaxDistanceCm, int32 InMaxEntries);

	/** Server-side only. Negative/zero means "not reported yet" - callers should fall back
	 *  to their own local config default in that case. Never replicated back down. */
	float GetDesiredMaxDistanceCm() const { return DesiredMaxDistanceCm; }
	int32 GetDesiredMaxEntries() const { return DesiredMaxEntries; }

private:
	UPROPERTY(Replicated)
	TArray<FVTCFedCollectible> Feed;

	float DesiredMaxDistanceCm = -1.f;
	int32 DesiredMaxEntries = -1;
};
