#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VTCConfig.h"
#include "VTCCategoryTables.h"
#include "VTCSelection.h"
#include "VTCCollectionState.h"
#include "VTCOutlineSubsystem.generated.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class UTexture2D;
class UMeshComponent;
class UVTCCollectibleFeedComponent;
class UVTCMapRepresentation;

/** Local presentation of one identity-based selection, shared by outlines, markers and map. */
UCLASS()
class VIEWTHROUGHCOLLECTIBLES_API UVTCOutlineSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	bool OwnsMapMarkerId(const FGuid& Id) const { return KnownMapMarkerIds.Contains(Id); }

private:
	friend class FVTCPresentationTest;
	void SetupPostProcess();
	void RefreshOutlines();
	void PushConfigToMaterial(const FVTCConfigStruct& Cfg);
	void LogConfigIfChanged(const FVTCConfigStruct& Cfg);

	bool ApplyCustomDepth(AActor* Actor, int32 Stencil, TSet<TWeakObjectPtr<UMeshComponent>>& Wanted);
	void ReleaseUnusedMeshes(const TSet<TWeakObjectPtr<UMeshComponent>>& Wanted);
	void ClearAllTrackedOutlines();
	void RefreshRemoteMarkers(const TArray<FVTCCandidate>& Selected);
	void ClearRemoteMarkers();
	void RefreshMapDots(const FVTCConfigStruct& Cfg, const TArray<FVTCCandidate>& Selected);
	void ClearMapDots();

	static int32 StencilFor(uint8 Category) { return 201 + Category; }

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> PostProcessVolume;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OutlineMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ColorCollection;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MarkerMesh;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DotTexture;

	FTimerHandle RefreshTimerHandle;
	float ArmedRefreshInterval = -1.f;
	FVTCCategoryTables Tables;
	TMap<TWeakObjectPtr<AActor>, FGuid> RuntimeIds;
	FVTCCollectionState CollectionState;

	struct FMeshState
	{
		bool OriginalEnabled = false;
		int32 OriginalStencil = 0;
		uint8 OriginalWriteMask = 0;
		int32 AppliedStencil = 0;
		bool StillOwned(const UMeshComponent* Mesh) const;
		void Restore(UMeshComponent* Mesh) const;
	};
	TMap<TWeakObjectPtr<UMeshComponent>, FMeshState> MeshStates;
	TMap<FGuid, TWeakObjectPtr<AActor>> RemoteMarkers;
	TMap<FGuid, TWeakObjectPtr<UVTCMapRepresentation>> MapDots;
	// Retain removed IDs until world teardown so a stale popup cannot save them
	// as shared markers after collection or a config change.
	TSet<FGuid> KnownMapMarkerIds;
	bool bMarkerMeshAttempted = false;
	bool bDotTextureAttempted = false;

	TWeakObjectPtr<UVTCCollectibleFeedComponent> ReportedFeed;
	FVTCFeedPreferences LastReportedPreferences;
	uint32 RequestRevision = 0;

	FVTCConfigStruct LastPushedConfig;
	bool bConfigEverPushed = false;
	FString LastLoggedConfigDigest;

	bool bOwnsJitterOverride = false;
	void ApplyCustomDepthJitterOverride();
	void RestoreCustomDepthJitterOverride();
};
