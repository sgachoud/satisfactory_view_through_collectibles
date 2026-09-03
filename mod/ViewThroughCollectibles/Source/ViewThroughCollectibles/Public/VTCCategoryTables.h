#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.h"

class AActor;

/**
 * Descriptor / actor-class -> collectible category lookup, seeded from built-in defaults plus
 * `Game.ini` `[ViewThroughCollectibles.Categories]` overrides. Shared by the client outline
 * subsystem and the server marker feed so both categorise collectibles the same way.
 *
 * Plain (non-UObject) — the maps only hold FSoftClassPath keys and enum values.
 */
struct FVTCCategoryTables
{
	/** (Re)build the tables. Call once after construction. */
	void Load();

	/**
	 * Resolve a collectible's category.
	 * @param Actor          the collectible actor if it is streamed in, else null
	 * @param FallbackClass  its class when the actor isn't available (from the scannable registry)
	 * Returns false if it matches no known category.
	 */
	bool Resolve(const AActor* Actor, const UClass* FallbackClass, EVTCCollectibleCategory& OutCategory) const;

private:
	TMap<FSoftClassPath, EVTCCollectibleCategory> ByDescriptor;
	TMap<FSoftClassPath, EVTCCollectibleCategory> ByActorClass;
};
