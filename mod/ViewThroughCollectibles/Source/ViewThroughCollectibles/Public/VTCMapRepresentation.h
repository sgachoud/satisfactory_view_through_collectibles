#pragma once

#include "CoreMinimal.h"
#include "FGActorRepresentation.h"
#include "VTCMapRepresentation.generated.h"

/**
 * Local map + compass representation for a collectible dot, created via
 * CreateAndAddNewRepresentationNoActor (colour + texture set directly from that call).
 *
 * GetScaleOnMap()/GetScaleWithMap() are virtual on the base specifically so a subclass can
 * pin the icon to a small, fixed (non map-zoom-scaling) size — RT_Default renders oversized
 * on its own.
 *
 * GetRepresentationCompassMaterial() is overridden to hand back the game's generic marker
 * compass material (UFGCompassMaterialSettings::mMarkerMaterial). Without it the compass
 * widget logs "Missing compass material" and skips the icon — the NoActor path only stores
 * a compass *texture*, not a material, and nothing builds one from it for a client rep.
 */
UCLASS()
class UVTCMapRepresentation : public UFGActorRepresentation
{
	GENERATED_BODY()

public:
	virtual bool GetScaleWithMap() const override { return false; }
	virtual float GetScaleOnMap() const override { return 0.3f; }
	virtual class UMaterialInterface* GetRepresentationCompassMaterial() const override;

	/** Label shown for this marker on the map and (when close) the compass. */
	void SetMarkerText(const FText& InText) { MarkerText = InText; }
	virtual FText GetRepresentationText() const override { return MarkerText; }

private:
	FText MarkerText;
};
