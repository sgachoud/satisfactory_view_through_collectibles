#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VTCTypes.h"
#include "VTCCollectibleFeedComponent.generated.h"

/** Owner-only, acknowledged pages keep large client selections below network bunch limits. */
UCLASS(ClassGroup = (Custom), NotBlueprintable)
class VIEWTHROUGHCOLLECTIBLES_API UVTCCollectibleFeedComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVTCCollectibleFeedComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void RequestPreferences(const FVTCFeedPreferences& Preferences, uint32 Revision);
	UFUNCTION(Server, Reliable)
	void Server_ReportPreferences(FVTCFeedPreferences Preferences, uint32 Revision);
	UFUNCTION(Server, Reliable)
	void Server_AcknowledgePage(uint32 Revision, uint32 Serial, int32 PageIndex);

	const FVTCFeedSnapshot& GetSnapshot() const { return Snapshot; }
	const FVTCFeedPreferences& GetPreferences() const { return RequestedPreferences; }
	bool IsRefreshDue(double Now) const { return RequestRevision != 0 && !bSending && Now >= NextRefreshTime; }
	void SetFeed(TArray<FVTCFedCollectible>&& Entries, double Now);

private:
	friend class FVTCPreferencesTest;
	UPROPERTY(ReplicatedUsing = OnRep_Page)
	FVTCFeedPage Page;
	UFUNCTION()
	void OnRep_Page();
	void SendPage(int32 Index);
	void ForceOwnerUpdate();

	FVTCFeedSnapshot Snapshot;
	FVTCFeedPreferences RequestedPreferences;
	uint32 RequestRevision = 0;
	double NextRefreshTime = 0.0;
	bool bSending = false;
	uint32 LastSentRevision = 0;
	TArray<FVTCFedCollectible> SendingEntries;

	uint32 ExpectedRevision = 0;
	uint32 ReceivingSerial = 0;
	int32 NextPageIndex = 0;
	TArray<FVTCFedCollectible> ReceivingEntries;
	static constexpr int32 EntriesPerPage = 128;
};