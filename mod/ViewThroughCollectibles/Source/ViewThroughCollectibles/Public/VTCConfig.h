#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.h"
#include "VTCConfig.generated.h"

/**
 * Mirror of the mod's SML configuration (asset: ViewThroughCollectibles_Config).
 *
 * Field names match the config's Root Section keys exactly, and each FVTCTypeSettings
 * matches a nested Section with keys `Enabled` + `Color`, so
 * UConfigManager::FillConfigurationStruct binds everything by name. Running SML's
 * "Generate C++ Configuration Header" is optional; if you do, keep the names identical to
 * these or update VTCOutlineSubsystem.cpp.
 */
USTRUCT(BlueprintType)
struct VIEWTHROUGHCOLLECTIBLES_API FVTCConfigStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
	float MaxDistanceMeters = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
	float RefreshIntervalSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
	int32 MaxSimultaneousOutlines = 200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float OutlineThicknessPixels = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float OccludedFillOpacity = 0.12f;

	/** Also drop a category-coloured dot on the map and compass for every active outline/marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	bool ShowOnMap = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings HardDrivePods{ true, TEXT("FF7300") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsBlue{ true, TEXT("1A8CFF") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsYellow{ true, TEXT("FFD91A") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsPurple{ true, TEXT("A633FF") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings MercerSpheres{ true, TEXT("1AFF8C") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Somersloops{ true, TEXT("FF1A59") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BerylNut{ false, TEXT("8CD933") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Paleberry{ false, TEXT("99CCF2") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BaconAgaric{ false, TEXT("E68C59") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings DroppedItems{ false, TEXT("F2F2F2") };

	/** Returns the settings block for a category. */
	const FVTCTypeSettings& GetFor(EVTCCollectibleCategory Category) const;

	/** Reads the live SML configuration into a struct. Safe to call every refresh tick. */
	static FVTCConfigStruct GetActiveConfig(const UObject* WorldContext);
};
