#include "VTCMapRepresentation.h"
#include "FGActorRepresentationManager.h"
#include "FGCompassMaterialSettings.h"
#include "FGMapManager.h"
#include "FGPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

ECompassViewDistance UVTCMapRepresentation::GetCompassViewDistance() const
{
	return bLocallyHighlighted ? ECompassViewDistance::CVD_Always : ECompassViewDistance::CVD_Off;
}

bool UVTCMapRepresentation::IsHighlighted(FLinearColor& OutColor, bool& bOutByLocalPlayer) const
{
	OutColor = bLocallyHighlighted ? mRepresentationColor : FLinearColor::Transparent;
	bOutByLocalPlayer = bLocallyHighlighted;
	return bLocallyHighlighted;
}

void UVTCMapRepresentation::SetHighlighted(bool bHighlighted)
{
	if (bLocallyHighlighted == bHighlighted) return;
	bLocallyHighlighted = bHighlighted;
	mShouldShowInCompass = bHighlighted;
	mMapMarker.CompassViewDistance = GetCompassViewDistance();

	// These representations exist only on this client. Native highlight replication
	// requires a server-known actor/map marker, so keep selection local in every mode.
	if (auto* Manager = GetTypedOuter<AFGActorRepresentationManager>())
	{
		Manager->UpdateRepresentation(this);
		// Also notify listeners specifically interested in visibility transitions.
		Manager->mOnActorRepresentationUpdatedCompassShow.Broadcast(this, bHighlighted);
		if (UWorld* World = Manager->GetWorld())
		{
			if (auto* MapManager = AFGMapManager::Get(World))
			{
				for (auto It = World->GetPlayerControllerIterator(); It; ++It)
				{
					APlayerController* PC = It->Get();
					if (PC && PC->IsLocalController())
					{
						MapManager->mOnMarkerHighlightUpdated.Broadcast(
							this, bHighlighted, PC->GetPlayerState<AFGPlayerState>());
						break;
					}
				}
			}
		}
	}
}

bool UVTCMapRepresentation::UpdateMarker(const FGuid& Id, const FVector& Location, const FLinearColor& Color, const FText& Label)
{
	if (mMapMarker.MarkerGUID == Id && mLocalActorLocation == Location &&
		mRepresentationColor == Color && mRepresentationText.EqualTo(Label))
		return false;
	// The native marker popup reads GetMapMarker(), then resolves its GUID via
	// AFGMapManager. A generic representation tagged RT_MapMarker cannot be found.
	mMapMarker.MarkerGUID = Id;
	mMapMarker.Location = Location;
	mMapMarker.Name = Label.ToString();
	mMapMarker.Color = Color;
	mMapMarker.Scale = GetScaleOnMap();
	mMapMarker.MapMarkerType = ERepresentationType::RT_MapMarker;
	mMapMarker.CompassViewDistance = GetCompassViewDistance();
	mLocalActorLocation = Location;
	mActorLocation = Location;
	mRepresentationColor = Color;
	mRepresentationText = Label;
	return true;
}

UMaterialInterface* UVTCMapRepresentation::GetRepresentationCompassMaterial() const
{
	if (const UFGCompassMaterialSettings* Settings = UFGCompassMaterialSettings::Get())
	{
		if (UMaterialInterface* Marker = Settings->mMarkerMaterial.LoadSynchronous()) return Marker;
		if (UMaterialInterface* Stamp = Settings->mStampMaterial.LoadSynchronous()) return Stamp;
	}
	return Super::GetRepresentationCompassMaterial();
}
