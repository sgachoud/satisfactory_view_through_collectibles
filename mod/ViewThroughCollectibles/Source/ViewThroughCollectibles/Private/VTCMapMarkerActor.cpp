#include "VTCMapMarkerActor.h"
#include "VTCMapRepresentation.h"

#include "FGActorRepresentationManager.h"
#include "FGActorRepresentation.h"

AVTCMapMarkerActor::AVTCMapMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
}

void AVTCMapMarkerActor::Init(UTexture2D* InIcon, const FLinearColor& InColor)
{
	Icon = InIcon;
	Color = InColor;
}

void AVTCMapMarkerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveAsRepresentation();
	Super::EndPlay(EndPlayReason);
}

ERepresentationType AVTCMapMarkerActor::GetActorRepresentationType()
{
	return ERepresentationType::RT_Default;
}

bool AVTCMapMarkerActor::AddAsRepresentation()
{
	AFGActorRepresentationManager* Mgr = AFGActorRepresentationManager::Get(GetWorld());
	if (!Mgr)
	{
		return false;
	}
	Representation = Mgr->CreateAndAddNewRepresentation(this, /*isLocal*/ true, UVTCMapRepresentation::StaticClass());
	return Representation != nullptr;
}

bool AVTCMapMarkerActor::UpdateRepresentation()
{
	AFGActorRepresentationManager* Mgr = AFGActorRepresentationManager::Get(GetWorld());
	if (!Mgr || !Representation)
	{
		return false;
	}
	return Mgr->UpdateRepresentation(Representation);
}

bool AVTCMapMarkerActor::RemoveAsRepresentation()
{
	if (Representation)
	{
		if (AFGActorRepresentationManager* Mgr = AFGActorRepresentationManager::Get(GetWorld()))
		{
			Mgr->RemoveRepresentation(Representation);
		}
		Representation = nullptr;
	}
	return true;
}
