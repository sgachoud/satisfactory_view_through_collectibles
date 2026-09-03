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
 * When the server also has the mod, a UVTCCollectibleFeedComponent on the local
 * PlayerController carries positions of collectibles that are too far to have streamed in;
 * those get a small see-through marker sphere (invisible except in the outline pass) until
 * the real actor loads.
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

	/** Server-fed distant collectibles -> transient marker spheres. No-op without a feed component. */
	void RefreshRemoteMarkers(const FVTCConfigStruct& Cfg, const FVector& PlayerLoc,
		const TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory>& LoadedOutlines);
	void ClearRemoteMarkers();

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

	/** Actor -> category currently written into Custom Depth. */
	TMap<TWeakObjectPtr<AActor>, EVTCCollectibleCategory> TrackedOutlines;

	/**
	 * Quantised world position -> the transient marker sphere placed there for a distant
	 * collectible. Not a UPROPERTY (uint64 keys aren't UHT-supported); the world owns the
	 * marker actors, we only weak-reference them.
	 */
	TMap<uint64, TWeakObjectPtr<AActor>> RemoteMarkers;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> MarkerMesh;

	FVTCCategoryTables Tables;

	/** Last values pushed to the MPC, to avoid redundant per-tick writes. */
	FVTCConfigStruct LastPushedConfig;
	bool bConfigEverPushed = false;

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
