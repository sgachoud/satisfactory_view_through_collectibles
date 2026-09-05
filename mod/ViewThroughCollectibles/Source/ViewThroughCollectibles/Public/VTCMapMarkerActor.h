#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGActorRepresentationInterface.h"
#include "VTCMapMarkerActor.generated.h"

class UFGActorRepresentation;

/**
 * Transient, client-only actor whose only job is to implement
 * IFGActorRepresentationInterface, so a distant collectible can register its map/compass
 * dot through the actor-backed path (AFGActorRepresentationManager::CreateAndAddNewRepresentation),
 * instead of the texture-argument "NoActor" convenience function — which rendered as an
 * untinted placeholder square regardless of the texture/colour/scale passed to it. The
 * interface is how the game's own actors (trains, vehicles, drones, ...) register their
 * representations, so the manager is expected to actually poll these getters.
 *
 * Never saved (transient, no IFGSaveInterface), never replicated, spawned and owned
 * entirely by the local client's UVTCOutlineSubsystem.
 */
UCLASS()
class AVTCMapMarkerActor : public AActor, public IFGActorRepresentationInterface
{
	GENERATED_BODY()

public:
	AVTCMapMarkerActor();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Call once right after spawning, before AddAsRepresentation(). */
	void Init(UTexture2D* InIcon, const FLinearColor& InColor);

	// Begin IFGActorRepresentationInterface
	virtual bool AddAsRepresentation() override;
	virtual bool UpdateRepresentation() override;
	virtual bool RemoveAsRepresentation() override;
	virtual bool IsActorStatic() override { return true; }
	virtual FVector GetRealActorLocation() override { return GetActorLocation(); }
	virtual UTexture2D* GetActorRepresentationTexture() override { return Icon; }
	virtual UMaterialInterface* GetActorRepresentationCompassMaterial() override { return nullptr; }
	virtual FText GetActorRepresentationText() override { return FText::GetEmpty(); }
	virtual FLinearColor GetActorRepresentationColor() override { return Color; }
	virtual ERepresentationType GetActorRepresentationType() override;
	virtual bool GetActorShouldShowInCompass() override { return true; }
	virtual bool GetActorShouldShowOnMap() override { return true; }
	// End IFGActorRepresentationInterface

private:
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Icon;

	FLinearColor Color = FLinearColor::White;

	UPROPERTY(Transient)
	TObjectPtr<UFGActorRepresentation> Representation;
};
