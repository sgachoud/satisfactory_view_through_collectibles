#pragma once

#include "CoreMinimal.h"
#include "VTCSelection.h"

class UWorld;
struct FVTCCategoryTables;
struct FVTCCollectionState;

namespace VTC
{
	/** Registry state is trusted only on authority. An empty registry is a valid result. */
	void GatherRegistry(UWorld* World, const FVTCCategoryTables& Tables,
		TMap<FGuid, FVTCCandidate>& Out);

	/** Loaded actors supply current meshes/state. When a server feed exists they may not
	 * reintroduce level-placed objects omitted by that authoritative selection. */
	void GatherLoaded(UWorld* World, const FVTCCategoryTables& Tables,
		TMap<TWeakObjectPtr<AActor>, FGuid>& RuntimeIds, TMap<FGuid, FVTCCandidate>& Out,
		FVTCCollectionState& CollectionState, bool bRequireServerRecord);
}
