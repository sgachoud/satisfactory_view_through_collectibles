#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VTCTypes.h"
#include "VTCConfig.h"
#include "VTCCategoryTables.h"
#include "VTCOutlineSubsystem.generated.h"

class AActor;
class APostProcessVolume;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMesh;
class UTexture2D;
class UFGActorRepresentation;

namespace VTC
{
	/** Custom Depth stencil values used by this mod are StencilBase + category + 1 (201..210). */
	inline constexpr int32 StencilBase = 200;
}

/**
 * Client-side, cosmetic-only subsystem.
 *
 * Draws a coloured, see-through silhouette on nearby collectibles by:
 *  - writing a per-category Custom Depth stencil value onto each collectible's mesh, and
 *  - blending a post-process material (M_VTCOutline) that turns those stencil values into
 *    outlines, with colours fed from a Material Parameter Collection (MPC_VTCColors).
 *
 * Collectibles within MaxDistanceMeters that aren't streamed in get a small see-through
 * marker sphere instead (invisible except in the outline pass). Their positions come from
 * the local AFGScannableSubsystem registry when we're the authority (solo / listen host),
 * or from a replicated UVTCCollectibleFeedComponent when we're a remote client and the
 * server also has the mod.
 *
 * Multiplayer: never runs on a dedicated server, never spawns/replicates a gameplay actor
 * (the post-process volume and marker spheres are transient and client-local), never
 * writes the save. Reads only already-replicated data. Fully independent per client.
 */
UCLASS()
class VIEWTHROUGHCOLLECTIBLES_API UVTCOutlineSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void SetupPostProcess();
	void RefreshOutlines();

	/** Push colours / thickness / fill from config into MPC_VTCColors (skips if unchanged). */
	void PushConfigToMaterial(const FVTCConfigStruct& Cfg);

	/** Enable/disable Custom Depth + set the stencil value on every mesh component of Actor. */
	static void ApplyCustomDepth(AActor* Actor, int32 StencilValue, bool bEnable);

	void ClearAllTrackedOutlines();

	/** Distant collectibles -> transient marker spheres. Sourced from RegistryNearby when
	 *  bHaveRegistry (authority), else from the replicated feed component (remote client). */
	void RefreshRemoteMarkers(const FVTCConfigStruct& Cfg, const FVector& PlayerLoc,
		const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines,
		const TArray<FVTCFedCollectible>& RegistryNearby, bool bHaveRegistry);
	void ClearRemoteMarkers();

	/** Category-coloured map/compass dots for everything currently outlined or markered. */
	void RefreshMapDots(const FVTCConfigStruct& Cfg,
		const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines);
	void ClearMapDots();
	UTexture2D* EnsureDotTexture();

	static int32 StencilFor(EVTCCollectibleCategory Category)
	{
		return VTC::StencilBase + static_cast<int32>(Category) + 1;
	}

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> PostProcessVolume;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OutlineMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ColorCollection;

	FTimerHandle RefreshTimerHandle;
	/** Interval the refresh timer is currently armed at; re-armed from config when it changes. */
	float ArmedRefreshInterval = -1.f;

	/** Actor -> category currently written into Custom Depth. */
	TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory> TrackedOutlines;

	/**
	 * Quantised world position -> the transient marker sphere placed there for a distant
	 * collectible, plus its location/category (for the map dots). Not a UPROPERTY (uint64
	 * keys aren't UHT-supported); the world owns the marker actors, we only weak-reference.
	 */
	struct FTrackedMarker
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Location = FVector::ZeroVector;
		EVTCCollectibleCategory Category = EVTCCollectibleCategory::HardDrivePod;

		FTrackedMarker() = default;
		FTrackedMarker(AActor* InActor, const FVector& InLoc, EVTCCollectibleCategory InCat)
			: Actor(InActor), Location(InLoc), Category(InCat) {}
	};
	TMap<uint64, FTrackedMarker> RemoteMarkers;

	/** Quantised world position -> the map/compass representation dot placed there. */
	TMap<uint64, TWeakObjectPtr<UFGActorRepresentation>> MapDots;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MarkerMesh;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DotTexture;

	FVTCCategoryTables Tables;

	/** Last values pushed to the MPC, to avoid redundant per-tick writes. */
	FVTCConfigStruct LastPushedConfig;
	bool bConfigEverPushed = false;

	/** Log the effective config once, and again whenever it changes, so binding is verifiable. */
	void LogConfigIfChanged(const FVTCConfigStruct& Cfg);
	FString LastLoggedConfigDigest;

	/**
	 * We force r.CustomDepthTemporalAAJitter to 0 while active so the outline stops
	 * shimmering (the Custom Depth pass otherwise inherits the TSR/TAA sub-pixel jitter).
	 * This holds the value to restore on Deinitialize; INT_MIN means "never changed it".
	 */
	int32 SavedCustomDepthJitter = MIN_int32;
	void ApplyCustomDepthJitterOverride();
	void RestoreCustomDepthJitterOverride();

	// Content asset paths (mount point is /<ModReference>/). Adjust if you move the assets.
	static const TCHAR* OutlineMaterialPath;
	static const TCHAR* ColorCollectionPath;
};
