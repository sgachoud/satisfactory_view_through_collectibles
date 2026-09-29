#pragma once

#include "VTCSelection.h"

/** Local observations outlive actor streaming and older server snapshots. Never saved. */
struct FVTCCollectionState
{
	void MergeLoaded(TMap<FGuid, FVTCCandidate>& Candidates, const FVTCCandidate& Loaded,
		bool bCollected, bool bCanRespawn, bool bRuntimeItem, bool bRequireServerRecord)
	{
		const FGuid& Id = Loaded.Data.Id;
		if (bCollected)
		{
			CollectedIds.Add(Id);
			Candidates.Remove(Id);
			return;
		}
		// Only an actually replenished, loaded respawnable pickup can undo this observation.
		if (bCanRespawn) CollectedIds.Remove(Id);
		if (CollectedIds.Contains(Id))
		{
			Candidates.Remove(Id);
			return;
		}
		// Server omission is authoritative for level-placed objects. A stale loaded actor
		// must not put a looted pod/pickup back into the map and highlight selection.
		// Runtime drops are not part of the registry feed and remain locally discoverable.
		if (bRequireServerRecord && !bRuntimeItem && !Candidates.Contains(Id)) return;
		Candidates.Add(Id, Loaded);
	}

	void Filter(TMap<FGuid, FVTCCandidate>& Candidates) const
	{
		for (const FGuid& Id : CollectedIds) Candidates.Remove(Id);
	}

	void ForgetRuntimeItem(const FGuid& Id) { CollectedIds.Remove(Id); }

private:
	TSet<FGuid> CollectedIds;
};
