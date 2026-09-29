#include "VTCConfig.h"
#include "Algo/AllOf.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

#include "Configuration/ConfigManager.h"     // also pulls FConfigId (ModConfiguration.h) + FDynamicStructInfo (ReflectionHelper.h)

FLinearColor FVTCTypeSettings::GetLinearColor() const
{
	FString Hex = Color;
	Hex.TrimStartAndEndInline();
	Hex.RemoveFromStart(TEXT("#"));
	if ((Hex.Len() != 3 && Hex.Len() != 6 && Hex.Len() != 8) ||
		!Algo::AllOf(Hex, [](TCHAR C) { return FChar::IsHexDigit(C); }))
	{
		return FLinearColor::White;
	}
	// FColor::FromHex handles RGB / RRGGBB / RRGGBBAA. It defaults alpha to 255 for 6-digit.
	const FColor SRGB = FColor::FromHex(Hex);
	return FLinearColor(SRGB);
}

FVTCFeedPreferences FVTCConfigStruct::FeedPreferences() const
{
	FVTCFeedPreferences Result;
	Result.MaxDistanceCm = MaxDistanceCm();
	Result.MaxEntries = SafeMaxSimultaneous();
	Result.RefreshSeconds = SafeRefreshIntervalSeconds();
	for (uint8 I = 0; I < static_cast<uint8>(EVTCCollectibleCategory::MAX); ++I)
	{
		if (GetFor(static_cast<EVTCCollectibleCategory>(I)).Enabled)
		{
			Result.EnabledCategories |= 1 << I;
		}
	}
	return Result;
}

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

	UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
	if (!ConfigManager)
	{
		return Config; // defaults
	}

	const FConfigId ConfigId{ TEXT("ViewThroughCollectibles"), TEXT("") };
	ConfigManager->FillConfigurationStruct(
		ConfigId,
		FDynamicStructInfo{ FVTCConfigStruct::StaticStruct(), &Config });

	return Config;
}
