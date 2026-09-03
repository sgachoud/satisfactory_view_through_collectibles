#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VTCTypes.h"
#include "VTCCollectibleFeedComponent.generated.h"

/**
 * Server -> owning-client channel for nearby collectibles the client probably hasn't
 * streamed in. The server's UVTCServerFeedSubsystem adds one of these to each
 * APlayerController and refreshes its list; it replicates only to that player's client
 * (a PlayerController is relevant to its owner alone). The client's UVTCOutlineSubsystem
 * reads it to place see-through markers.
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

private:
	UPROPERTY(Replicated)
	TArray<FVTCFedCollectible> Feed;
};
