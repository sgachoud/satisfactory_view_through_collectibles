#include "VTCCollectibleFeedComponent.h"

#include "Net/UnrealNetwork.h"

UVTCCollectibleFeedComponent::UVTCCollectibleFeedComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UVTCCollectibleFeedComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVTCCollectibleFeedComponent, Feed);
}

void UVTCCollectibleFeedComponent::SetFeed(TArray<FVTCFedCollectible>&& NewFeed)
{
	// RepLayout diffs against the shadow buffer, so re-assigning an identical list costs a
	// comparison but no bandwidth.
	Feed = MoveTemp(NewFeed);
}

void UVTCCollectibleFeedComponent::Server_ReportRangePreference_Implementation(float InMaxDistanceCm, int32 InMaxEntries)
{
	DesiredMaxDistanceCm = InMaxDistanceCm;
	DesiredMaxEntries = InMaxEntries;
}
