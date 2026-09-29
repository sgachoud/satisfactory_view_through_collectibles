#include "VTCCollectibleFeedComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UVTCCollectibleFeedComponent::UVTCCollectibleFeedComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UVTCCollectibleFeedComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVTCCollectibleFeedComponent, Page, COND_OwnerOnly);
}

void UVTCCollectibleFeedComponent::RequestPreferences(const FVTCFeedPreferences& Preferences, uint32 Revision)
{
	ExpectedRevision = Revision;
	Snapshot = {};
	ReceivingEntries.Reset();
	NextPageIndex = 0;
	Server_ReportPreferences(Preferences, Revision);
}

void UVTCCollectibleFeedComponent::Server_ReportPreferences_Implementation(
	FVTCFeedPreferences Preferences, uint32 Revision)
{
	// No host defaults or hidden distance/count caps. Requests schedule bounded work;
	// they never scan the registry synchronously inside an RPC.
	if (!Preferences.IsValid() || Revision == 0 || Revision == RequestRevision) return;
	RequestedPreferences = Preferences;
	RequestRevision = Revision;
	NextRefreshTime = 0.0;
	bSending = false;
}

void UVTCCollectibleFeedComponent::ForceOwnerUpdate()
{
	if (AActor* Owner = GetOwner()) Owner->ForceNetUpdate();
}

void UVTCCollectibleFeedComponent::SetFeed(TArray<FVTCFedCollectible>&& Entries, double Now)
{
	NextRefreshTime = Now + RequestedPreferences.RefreshSeconds;
	if (LastSentRevision == RequestRevision && SendingEntries == Entries) return;
	SendingEntries = MoveTemp(Entries);
	LastSentRevision = RequestRevision;
	Page.Revision = RequestRevision;
	if (++Page.Serial == 0) ++Page.Serial;
	bSending = true;
	SendPage(0);
}

void UVTCCollectibleFeedComponent::SendPage(int32 Index)
{
	Page.Index = Index;
	const int32 Start = Index * EntriesPerPage;
	const int32 Count = FMath::Min(EntriesPerPage, SendingEntries.Num() - Start);
	Page.Entries.Reset(Count);
	for (int32 I = 0; I < Count; ++I) Page.Entries.Add(SendingEntries[Start + I]);
	Page.bLast = Start + Count == SendingEntries.Num();
	ForceOwnerUpdate();
}

void UVTCCollectibleFeedComponent::Server_AcknowledgePage_Implementation(
	uint32 Revision, uint32 Serial, int32 PageIndex)
{
	if (!bSending || Revision != RequestRevision || Serial != Page.Serial || PageIndex != Page.Index) return;
	if (Page.bLast)
	{
		bSending = false;
	}
	else
	{
		SendPage(Page.Index + 1);
	}
}

void UVTCCollectibleFeedComponent::OnRep_Page()
{
	// Property replication can race a new local settings report.
	if (Page.Revision != ExpectedRevision || Page.Revision == 0) return;
	if (Page.Index == 0)
	{
		ReceivingEntries.Reset();
		ReceivingSerial = Page.Serial;
		NextPageIndex = 0;
	}
	if (Page.Serial != ReceivingSerial || Page.Index != NextPageIndex) return;
	ReceivingEntries.Append(Page.Entries);
	++NextPageIndex;
	if (Page.bLast)
	{
		Snapshot.Revision = Page.Revision;
		Snapshot.Entries = MoveTemp(ReceivingEntries);
	}
	// Only one bounded page is in flight per owner; slow links cannot accumulate
	// an unbounded reliable RPC queue. Publish only complete snapshots.
	if (GetOwner()) Server_AcknowledgePage(Page.Revision, Page.Serial, Page.Index);
}