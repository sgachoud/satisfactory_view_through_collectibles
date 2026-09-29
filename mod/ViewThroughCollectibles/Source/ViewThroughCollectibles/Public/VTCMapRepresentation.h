#pragma once

#include "CoreMinimal.h"
#include "Representation/FGMapMarkerRepresentation.h"
#include "VTCMapRepresentation.generated.h"

/** Local map dot, visible on the compass only while explicitly highlighted. */
UCLASS()
class UVTCMapRepresentation : public UFGMapMarkerRepresentation
{
	GENERATED_BODY()
public:
	virtual bool GetScaleWithMap() const override { return false; }
	virtual float GetScaleOnMap() const override { return 1.0f; }
	virtual bool GetShouldShowInCompass() const override { return bLocallyHighlighted; }
	virtual ECompassViewDistance GetCompassViewDistance() const override;
	virtual bool CanBeHighlighted() const override { return true; }
	virtual void SetHighlighted(bool bHighlighted) override;
	virtual bool IsHighlighted() const override { return bLocallyHighlighted; }
	virtual bool IsHighlighted(FLinearColor& OutColor, bool& bOutByLocalPlayer) const override;
	virtual UMaterialInterface* GetRepresentationCompassMaterial() const override;
	virtual FVector GetActorLocation() const override { return mLocalActorLocation; }
	virtual FLinearColor GetRepresentationColor() const override { return mRepresentationColor; }
	virtual FText GetRepresentationText() const override { return mRepresentationText; }
	bool UpdateMarker(const FGuid& Id, const FVector& Location, const FLinearColor& Color, const FText& Label);

private:
	bool bLocallyHighlighted = false;
};
