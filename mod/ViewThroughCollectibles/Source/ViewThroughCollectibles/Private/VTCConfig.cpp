#include "VTCConfig.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// SML configuration runtime.
// TODO(needs SML source access): these include paths and the ConfigManager API are correct
// for recent SML but should be re-checked against the SML version you build against. If the
// in-editor Configuration Tool generates its own GetActiveConfig(), prefer that body.
#include "Configuration/ConfigManager.h"
#include "Configuration/ConfigId.h"

const FVTCTypeSettings& FVTCConfigStruct::GetFor(EVTCCollectibleCategory Category) const
{
	switch (Category)
	{
	case EVTCCollectibleCategory::HardDrivePod: return HardDrivePods;
	case EVTCCollectibleCategory::PowerSlugMk1: return PowerSlugsBlue;
	case EVTCCollectibleCategory::PowerSlugMk2: return PowerSlugsYellow;
	case EVTCCollectibleCategory::PowerSlugMk3: return PowerSlugsPurple;
	case EVTCCollectibleCategory::MercerSphere: return MercerSpheres;
	case EVTCCollectibleCategory::Somersloop:   return Somersloops;
	case EVTCCollectibleCategory::BerylNut:     return BerylNut;
	case EVTCCollectibleCategory::Paleberry:    return Paleberry;
	case EVTCCollectibleCategory::BaconAgaric:  return BaconAgaric;
	case EVTCCollectibleCategory::DroppedItem:  return DroppedItems;
	default:                                    return DroppedItems;
	}
}

FVTCConfigStruct FVTCConfigStruct::GetActiveConfig(const UObject* WorldContext)
{
	FVTCConfigStruct Config{};

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || !World->GetGameInstance())
	{
		return Config; // defaults
	}

	UConfigManager* ConfigManager =
		World->GetGameInstance()->GetSubsystem<UConfigManager>();
	if (!ConfigManager)
	{
		return Config; // defaults
	}

	// Root config id: { modReference, "" }. modReference must match the .uplugin name.
	const FConfigId ConfigId{ TEXT("ViewThroughCollectibles"), TEXT("") };
	ConfigManager->FillConfigStruct(
		ConfigId,
		FDynamicStructInfo{ FVTCConfigStruct::StaticStruct(), &Config });

	return Config;
}
