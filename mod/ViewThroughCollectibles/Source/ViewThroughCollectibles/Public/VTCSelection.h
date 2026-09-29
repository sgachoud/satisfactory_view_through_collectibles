#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.h"

class AActor;

/** Identity is independent of position, category, and whether the actor is loaded. */
struct FVTCCandidate
{
	FVTCFedCollectible Data;
	TWeakObjectPtr<AActor> Actor;
};

namespace VTC
{
	inline bool IdLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	/** Filter first, then apply one deterministic nearest-N cap on server and client. */
	inline TArray<FVTCCandidate> SelectCollectibles(const TMap<FGuid, FVTCCandidate>& Candidates,
		const FVector& Origin, const FVTCFeedPreferences& Preferences)
	{
		TArray<FVTCCandidate> Selected;
		if (!Preferences.IsValid() || Preferences.MaxEntries == 0 || Preferences.MaxDistanceCm == 0.0)
		{
			return Selected;
		}
		const double RadiusSquared = FMath::Square(Preferences.MaxDistanceCm);
		for (const auto& Pair : Candidates)
		{
			const FVTCFedCollectible& Data = Pair.Value.Data;
			if (Data.Id.IsValid() && Preferences.Includes(Data.Category) && !Data.Location.ContainsNaN() &&
				FVector::DistSquared(Data.Location, Origin) <= RadiusSquared)
			{
				Selected.Add(Pair.Value);
			}
		}
		Selected.Sort([&](const FVTCCandidate& A, const FVTCCandidate& B)
		{
			const double AD = FVector::DistSquared(A.Data.Location, Origin);
			const double BD = FVector::DistSquared(B.Data.Location, Origin);
			return AD == BD ? IdLess(A.Data.Id, B.Data.Id) : AD < BD;
		});
		if (Selected.Num() > Preferences.MaxEntries)
		{
			Selected.SetNum(Preferences.MaxEntries, EAllowShrinking::No);
		}
		return Selected;
	}
}
