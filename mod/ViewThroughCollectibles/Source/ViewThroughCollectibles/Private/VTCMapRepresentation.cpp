#include "VTCMapRepresentation.h"

#include "FGCompassMaterialSettings.h"
#include "Materials/MaterialInterface.h"

UMaterialInterface* UVTCMapRepresentation::GetRepresentationCompassMaterial() const
{
	// The game's generic marker compass material (the one all user map markers use). It reads
	// its icon + tint from the representation's stored texture/colour, which the
	// CreateAndAddNewRepresentationNoActor call sets for us.
	if (const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get())
	{
		if (UMaterialInterface* Marker = Settings->mMarkerMaterial.LoadSynchronous())
		{
			return Marker;
		}
		if (UMaterialInterface* Stamp = Settings->mStampMaterial.LoadSynchronous())
		{
			return Stamp;
		}
	}
	return Super::GetRepresentationCompassMaterial();
}
