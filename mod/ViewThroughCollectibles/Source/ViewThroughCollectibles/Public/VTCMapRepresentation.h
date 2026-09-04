#pragma once

#include "CoreMinimal.h"
#include "FGActorRepresentation.h"
#include "VTCMapRepresentation.generated.h"

/**
 * Map/compass representation for a collectible dot.
 *
 * CreateAndAddNewRepresentationNoActor's default (RT_Default, no representation class)
 * renders as a large, untinted square — RT_Default has no small-icon styling of its own.
 * UFGActorRepresentation::GetScaleOnMap()/GetScaleWithMap() are virtual specifically so a
 * subclass can override them (see UFGMapMarkerRepresentation), so this pins the map scale
 * to a small, fixed (non map-zoom-scaling) size. Colour and texture still come from the
 * base class's stored mRepresentationColor / mRepresentationTexture, set from the
 * CreateAndAddNewRepresentationNoActor call.
 */
UCLASS()
class UVTCMapRepresentation : public UFGActorRepresentation
{
	GENERATED_BODY()

public:
	virtual bool GetScaleWithMap() const override { return false; }
	virtual float GetScaleOnMap() const override { return 0.3f; }
};
