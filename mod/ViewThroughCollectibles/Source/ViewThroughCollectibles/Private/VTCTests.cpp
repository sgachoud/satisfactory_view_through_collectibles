#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "VTCSelection.h"
#include "VTCConfig.h"
#include "VTCCollectibleFeedComponent.h"
#include "VTCOutlineSubsystem.h"
#include "VTCMapRepresentation.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Components/StaticMeshComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVTCSelectionTest, "ViewThroughCollectibles.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVTCSelectionTest::RunTest(const FString&)
{
	TMap<FGuid, FVTCCandidate> Candidates;
	auto Add = [&](uint32 Id, double Distance, EVTCCollectibleCategory Category)
	{
		FVTCCandidate C;
		C.Data.Id = FGuid(0, 0, 0, Id);
		C.Data.Location = FVector(Distance, 0, 0);
		C.Data.Category = static_cast<uint8>(Category);
		Candidates.Add(C.Data.Id, C);
	};
	Add(1, 10, EVTCCollectibleCategory::BerylNut);
	Add(2, 20, EVTCCollectibleCategory::HardDrivePod);
	Add(3, 20, EVTCCollectibleCategory::HardDrivePod);
	Add(4, 90, EVTCCollectibleCategory::PowerSlugMk1);
	FVTCFeedPreferences P;
	P.MaxDistanceCm = 100;
	P.MaxEntries = 2;
	P.EnabledCategories = (1 << 0) | (1 << 1);
	auto Selected = VTC::SelectCollectibles(Candidates, FVector::ZeroVector, P);
	if (!TestEqual(TEXT("One combined cap"), Selected.Num(), 2)) return false;
	TestEqual(TEXT("Disabled nearby flora cannot consume budget"), Selected[0].Data.Id, FGuid(0, 0, 0, 2));
	TestEqual(TEXT("Distinct colocated objects survive with stable tie order"), Selected[1].Data.Id, FGuid(0, 0, 0, 3));
	Add(2, 15, EVTCCollectibleCategory::HardDrivePod);
	TestEqual(TEXT("A loaded record replaces the same feed identity"), Candidates.Num(), 4);
	P.MaxEntries = 1000;
	TestEqual(TEXT("No hidden server floor or cap"), VTC::SelectCollectibles(Candidates, FVector::ZeroVector, P).Num(), 3);
	P.MaxEntries = 0;
	TestTrue(TEXT("Zero count disables selection"), VTC::SelectCollectibles(Candidates, FVector::ZeroVector, P).IsEmpty());
	P.MaxEntries = 2;
	P.MaxDistanceCm = 0;
	TestTrue(TEXT("Zero distance disables selection"), VTC::SelectCollectibles(Candidates, FVector::ZeroVector, P).IsEmpty());
	P.MaxDistanceCm = std::numeric_limits<double>::infinity();
	TestFalse(TEXT("Infinite requests rejected"), P.IsValid());
	P.MaxDistanceCm = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("NaN requests rejected"), P.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVTCPreferencesTest, "ViewThroughCollectibles.ClientPreferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVTCPreferencesTest::RunTest(const FString&)
{
	auto* A = NewObject<UVTCCollectibleFeedComponent>();
	auto* B = NewObject<UVTCCollectibleFeedComponent>();
	FVTCConfigStruct Config;
	Config.MaxDistanceMeters = 4321;
	Config.MaxSimultaneousOutlines = 1234;
	Config.RefreshIntervalSeconds = 0.2f;
	Config.HardDrivePods.Enabled = false;
	const auto Preferences = Config.FeedPreferences();
	TestFalse(TEXT("Server waits for first client request"), A->IsRefreshDue(100));
	A->Server_ReportPreferences_Implementation(Preferences, 7);
	B->Server_ReportPreferences_Implementation(FVTCConfigStruct().FeedPreferences(), 1);
	TestTrue(TEXT("Client preferences preserved exactly"), A->GetPreferences() == Preferences);
	TestFalse(TEXT("Clients are independent"), A->GetPreferences() == B->GetPreferences());
	A->SetFeed({}, 100);
	TestEqual(TEXT("Even an empty result acknowledges the request"), A->Page.Revision, uint32(7));
	A->Server_AcknowledgePage_Implementation(A->Page.Revision, A->Page.Serial, A->Page.Index);
	TestFalse(TEXT("Refresh follows this client's interval"), A->IsRefreshDue(100.1));
	TestTrue(TEXT("Refresh becomes due"), A->IsRefreshDue(100.3));
	FVTCFeedPreferences Invalid = Preferences;
	Invalid.MaxEntries = -1;
	A->Server_ReportPreferences_Implementation(Invalid, 8);
	TestTrue(TEXT("Invalid RPC cannot replace valid preferences"), A->GetPreferences() == Preferences);
	FVTCFeedPreferences Zero = Preferences;
	Zero.MaxEntries = 0;
	Zero.MaxDistanceCm = 0;
	A->Server_ReportPreferences_Implementation(Zero, 9);
	TestTrue(TEXT("Zero does not mean server defaults"), A->GetPreferences() == Zero);
	TestTrue(TEXT("Changed configuration schedules next server tick"), A->IsRefreshDue(100.1));
	auto* Receiver = NewObject<UVTCCollectibleFeedComponent>();
	Receiver->ExpectedRevision = 10;
	A->Server_ReportPreferences_Implementation(Preferences, 10);
	TArray<FVTCFedCollectible> LargeFeed;
	for (uint32 I = 1; I <= 300; ++I)
	{
		FVTCFedCollectible Entry;
		Entry.Id = FGuid(0, 0, 0, I);
		Entry.Location = FVector(I, 0, 0);
		LargeFeed.Add(Entry);
	}
	A->SetFeed(MoveTemp(LargeFeed), 200);
	int32 Pages = 0;
	while (A->bSending && Pages < 10)
	{
		TestTrue(TEXT("Page stays within network budget"), A->Page.Entries.Num() <= 128);
		Receiver->Page = A->Page;
		Receiver->OnRep_Page();
		if (!A->Page.bLast)
			TestTrue(TEXT("Partial snapshot is never exposed"), Receiver->GetSnapshot().Entries.IsEmpty());
		A->Server_AcknowledgePage_Implementation(A->Page.Revision, A->Page.Serial, A->Page.Index);
		++Pages;
	}
	TestEqual(TEXT("Large feed arrives in bounded pages"), Pages, 3);
	TestEqual(TEXT("Full requested selection is preserved"), Receiver->GetSnapshot().Entries.Num(), 300);
	TestEqual(TEXT("Completed snapshot has request revision"), Receiver->GetSnapshot().Revision, uint32(10));
	Receiver->ExpectedRevision = 11;
	Receiver->Snapshot = {};
	Receiver->OnRep_Page();
	TestTrue(TEXT("Old response cannot restore a previous selection"), Receiver->GetSnapshot().Entries.IsEmpty());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVTCCollectedStateTest, "ViewThroughCollectibles.CollectedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVTCCollectedStateTest::RunTest(const FString&)
{
	FVTCCollectionState State;
	FVTCCandidate Pod;
	Pod.Data.Id = FGuid(1, 0, 0, 1);
	Pod.Data.Category = static_cast<uint8>(EVTCCollectibleCategory::HardDrivePod);
	Pod.Data.Location = FVector(100, 0, 0);
	TMap<FGuid, FVTCCandidate> Candidates;

	State.MergeLoaded(Candidates, Pod, false, false, false, true);
	TestTrue(TEXT("An empty server result cannot be repopulated by a stale loaded pod"), Candidates.IsEmpty());

	Candidates.Add(Pod.Data.Id, Pod);
	State.MergeLoaded(Candidates, Pod, false, false, false, true);
	TestTrue(TEXT("An available server record can use the loaded actor"), Candidates.Contains(Pod.Data.Id));
	State.MergeLoaded(Candidates, Pod, true, false, false, true);
	TestTrue(TEXT("A newly looted pod is removed before either presentation pass"), Candidates.IsEmpty());

	// Next refresh: the pod is unloaded, but a previous server snapshot still includes it.
	Candidates.Add(Pod.Data.Id, Pod);
	State.Filter(Candidates);
	TestTrue(TEXT("Collection memory survives unloading and stale server snapshots"), Candidates.IsEmpty());

	Candidates.Add(Pod.Data.Id, Pod);
	State.MergeLoaded(Candidates, Pod, false, false, false, true);
	TestTrue(TEXT("A stale uncollected actor cannot revive a non-respawning collectible"), Candidates.IsEmpty());
	TestTrue(TEXT("Collected objects consume no map/outline budget"),
		VTC::SelectCollectibles(Candidates, FVector::ZeroVector, FVTCConfigStruct().FeedPreferences()).IsEmpty());

	FVTCCandidate Plant = Pod;
	Plant.Data.Id = FGuid(1, 0, 0, 2);
	Plant.Data.Category = static_cast<uint8>(EVTCCollectibleCategory::BerylNut);
	Candidates.Add(Plant.Data.Id, Plant);
	State.MergeLoaded(Candidates, Plant, true, true, false, true);
	TestFalse(TEXT("Harvested plant is hidden"), Candidates.Contains(Plant.Data.Id));
	Candidates.Add(Plant.Data.Id, Plant);
	State.MergeLoaded(Candidates, Plant, false, true, false, true);
	TestTrue(TEXT("Actually regrown, server-listed plants can return"), Candidates.Contains(Plant.Data.Id));

	FVTCCandidate Drop = Pod;
	Drop.Data.Id = FGuid(1, 0, 0, 3);
	Drop.Data.Category = static_cast<uint8>(EVTCCollectibleCategory::DroppedItem);
	Candidates.Reset();
	State.MergeLoaded(Candidates, Drop, false, false, true, true);
	TestTrue(TEXT("Runtime dropped items do not require a registry record"), Candidates.Contains(Drop.Data.Id));
	State.MergeLoaded(Candidates, Drop, true, false, true, true);
	TestTrue(TEXT("Collected dropped item is removed too"), Candidates.IsEmpty());

	FVTCCandidate Slug = Pod;
	Slug.Data.Id = FGuid(1, 0, 0, 4);
	Slug.Data.Category = static_cast<uint8>(EVTCCollectibleCategory::PowerSlugMk1);
	State.MergeLoaded(Candidates, Slug, false, false, false, false);
	TestTrue(TEXT("Solo/unmodded-server loaded discovery remains available"), Candidates.Contains(Slug.Data.Id));
	State.MergeLoaded(Candidates, Slug, true, false, false, false);
	TestTrue(TEXT("Collection filtering also applies without a server feed"), Candidates.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVTCConfigTest, "ViewThroughCollectibles.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVTCConfigTest::RunTest(const FString&)
{
	FVTCTypeSettings Type;
	Type.Color = TEXT("not-a-colour");
	TestEqual(TEXT("Malformed hex falls back to white"), Type.GetLinearColor(), FLinearColor::White);
	Type.Color = TEXT("12GG00");
	TestEqual(TEXT("Invalid digits fall back to white"), Type.GetLinearColor(), FLinearColor::White);
	Type.Color = TEXT("#FF000080");
	TestTrue(TEXT("Valid alpha is preserved"), FMath::IsNearlyEqual(Type.GetLinearColor().A, 128.f / 255.f));
	FVTCConfigStruct Config;
	Config.MaxDistanceMeters = std::numeric_limits<float>::quiet_NaN();
	Config.RefreshIntervalSeconds = std::numeric_limits<float>::infinity();
	TestTrue(TEXT("Invalid saved config produces a valid query"), Config.FeedPreferences().IsValid());
	TestEqual(TEXT("Invalid distance fails closed"), Config.MaxDistanceCm(), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVTCPresentationTest, "ViewThroughCollectibles.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVTCPresentationTest::RunTest(const FString&)
{
	UWorld::InitializationValues IVS;
	IVS.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr,
		true, ERHIFeatureLevel::SM5, &IVS);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	auto* Subsystem = NewObject<UVTCOutlineSubsystem>(World);
	if (auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")))
	{
		const int32 Before = CVar->GetInt();
		const EConsoleVariableFlags FlagsBefore = CVar->GetFlags();
		auto* OverlappingWorldSubsystem = NewObject<UVTCOutlineSubsystem>(World);
		Subsystem->ApplyCustomDepthJitterOverride();
		OverlappingWorldSubsystem->ApplyCustomDepthJitterOverride();
		const int32 WhileActive = CVar->GetInt();
		Subsystem->RestoreCustomDepthJitterOverride();
		TestEqual(TEXT("First world cannot remove the second world's override"), CVar->GetInt(), WhileActive);
		OverlappingWorldSubsystem->RestoreCustomDepthJitterOverride();
		TestEqual(TEXT("Console value restored"), CVar->GetInt(), Before);
		TestEqual(TEXT("Console priority and flags restored"), CVar->GetFlags(), FlagsBefore);
	}
	FVTCCandidate C;
	C.Data.Id = FGuid::NewGuid();
	C.Data.Location = FVector(10000, 20000, 30000);
	C.Data.Category = 1;
	TArray<FVTCCandidate> Selected{C};
	Subsystem->RefreshRemoteMarkers(Selected);
	AActor* Marker = Subsystem->RemoteMarkers.FindRef(C.Data.Id).Get();
	if (TestNotNull(TEXT("Marker created"), Marker))
	{
		TestEqual(TEXT("Marker is at collectible position"), Marker->GetActorLocation(), FVector(C.Data.Location));
		C.Data.Location = FVector(11000, 21000, 31000);
		Selected[0] = C;
		Subsystem->RefreshRemoteMarkers(Selected);
		TestEqual(TEXT("Identity survives movement"), Subsystem->RemoteMarkers.FindRef(C.Data.Id).Get(), Marker);
		TestEqual(TEXT("Marker position updates"), Marker->GetActorLocation(), FVector(C.Data.Location));
	}

	AActor* Actor = World->SpawnActor<AActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Actor->SetRootComponent(Mesh);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Mesh->SetCustomDepthStencilValue(42);
	Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_1);
	Mesh->RegisterComponent();
	TSet<TWeakObjectPtr<UMeshComponent>> Wanted;
	TestTrue(TEXT("Mesh outlined"), Subsystem->ApplyCustomDepth(Actor, 202, Wanted));
	Subsystem->ClearAllTrackedOutlines();
	TestFalse(TEXT("Original depth flag restored"), bool(Mesh->bRenderCustomDepth));
	TestEqual(TEXT("Original stencil restored"), Mesh->CustomDepthStencilValue, 42);
	TestEqual(TEXT("Original mask restored"), Mesh->CustomDepthStencilWriteMask, ERendererStencilMask::ERSM_1);
	Subsystem->ApplyCustomDepth(Actor, 202, Wanted);
	Mesh->SetCustomDepthStencilValue(99);
	Subsystem->ClearAllTrackedOutlines();
	TestEqual(TEXT("Another system's stencil is preserved"), Mesh->CustomDepthStencilValue, 99);
	TestTrue(TEXT("Another system's depth flag is preserved"), bool(Mesh->bRenderCustomDepth));
	TestFalse(TEXT("Existing custom depth is not claimed"), Subsystem->ApplyCustomDepth(Actor, 202, Wanted));

	auto* Rep = NewObject<UVTCMapRepresentation>();
	TestTrue(TEXT("Local collectible markers support highlighting"), Rep->CanBeHighlighted());
	TestFalse(TEXT("Unselected dots stay off the compass"), Rep->GetShouldShowInCompass());
	Rep->SetHighlighted(true);
	TestTrue(TEXT("Highlighting adds the compass indicator"), Rep->GetShouldShowInCompass());
	TestTrue(TEXT("Highlighted target is visible at any distance"),
		Rep->GetCompassViewDistance() == ECompassViewDistance::CVD_Always);
	Rep->UpdateMarker(C.Data.Id, FVector(10, 20, 30), FLinearColor::Red, FText::FromString(TEXT("Pod")));
	// The popup requires this native class and a usable GUID, not just RT_MapMarker.
	auto* NativeMarker = Cast<UFGMapMarkerRepresentation>(Rep);
	if (!TestNotNull(TEXT("Native marker popup can read the collectible"), NativeMarker)) return false;
	TestEqual(TEXT("Popup receives the collectible's persistent identity"), NativeMarker->GetMapMarker().MarkerGUID, C.Data.Id);
	TestTrue(TEXT("Highlighted popup reports compass visibility"),
		NativeMarker->GetMapMarker().CompassViewDistance == ECompassViewDistance::CVD_Always);
	TestTrue(TEXT("Live colour changes update existing dot"),
		Rep->UpdateMarker(C.Data.Id, FVector(10, 20, 30), FLinearColor::Green, FText::FromString(TEXT("Pod"))));
	TestEqual(TEXT("Dot exposes new colour"), Rep->GetRepresentationColor(), FLinearColor::Green);
	TestEqual(TEXT("Refreshing a selected dot preserves popup identity"), NativeMarker->GetMapMarker().MarkerGUID, C.Data.Id);
	TestEqual(TEXT("Popup reads the current configured colour"), NativeMarker->GetMapMarker().Color, FLinearColor::Green);
	FLinearColor HighlightColor;
	bool bHighlightedLocally = false;
	TestTrue(TEXT("Appearance refresh preserves selection"), Rep->IsHighlighted(HighlightColor, bHighlightedLocally));
	TestTrue(TEXT("Selection belongs to the local player"), bHighlightedLocally);
	TestEqual(TEXT("Selection reflects the new colour"), HighlightColor, FLinearColor::Green);
	Rep->SetHighlighted(false);
	TestTrue(TEXT("Unhighlighted popup reports compass off"),
		NativeMarker->GetMapMarker().CompassViewDistance == ECompassViewDistance::CVD_Off);
	TestFalse(TEXT("Deselecting removes the compass indicator"), Rep->GetShouldShowInCompass());
	TestFalse(TEXT("Deselecting clears highlight metadata"), Rep->IsHighlighted(HighlightColor, bHighlightedLocally));
	TestFalse(TEXT("Deselecting clears local ownership"), bHighlightedLocally);
	Rep->SetHighlighted(true);
	TestFalse(TEXT("Unchanged dot needs no update"),
		Rep->UpdateMarker(C.Data.Id, FVector(10, 20, 30), FLinearColor::Green, FText::FromString(TEXT("Pod"))));

	Mesh->SetRenderCustomDepth(false);
	Subsystem->ApplyCustomDepth(Actor, 202, Wanted);
	Subsystem->MapDots.Add(C.Data.Id, Rep);
	Subsystem->KnownMapMarkerIds.Add(C.Data.Id);
	Subsystem->RefreshOutlines(); // No local pawn: all presentation must be removed.
	TestTrue(TEXT("Pawn loss clears markers"), Subsystem->RemoteMarkers.IsEmpty());
	TestTrue(TEXT("Pawn loss clears map dots"), Subsystem->MapDots.IsEmpty());
	TestFalse(TEXT("Removing a selected dot clears its compass indicator"), Rep->GetShouldShowInCompass());
	TestTrue(TEXT("Removed marker IDs remain local for stale popup actions"), Subsystem->OwnsMapMarkerId(C.Data.Id));
	TestFalse(TEXT("Ordinary player marker IDs are not claimed"), Subsystem->OwnsMapMarkerId(FGuid::NewGuid()));
	TestTrue(TEXT("Pawn loss clears mesh ownership"), Subsystem->MeshStates.IsEmpty());
	Subsystem->Deinitialize();
	World->DestroyWorld(false);
	return true;
}
#endif
