#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.h"
#include "VTCConfig.generated.h"

/**
 * Mirror of the mod's SML configuration.
 *
 * SML's in-editor "Configuration Tool" generates a UModConfiguration asset (+ its settings
 * widget) from a struct of this exact shape, and normally also generates the
 * GetActiveConfig() accessor. We keep a hand-written struct + accessor here so the C++ side
 * compiles and is reviewable without the editor; when you generate the asset, either keep
 * this struct as the generation source or replace the accessor body below with the
 * generated one if the SML ConfigManager signature has drifted.
 *
 * TODO(needs FactoryGame / SML source access): verify FConfigId construction and
 * UConfigManager::FillConfigStruct against the SML version you build against.
 */
USTRUCT(BlueprintType)
struct VIEWTHROUGHCOLLECTIBLES_API FVTCConfigStruct
{
	GENERATED_BODY()

	/** Collectibles farther than this from the local player are not outlined. Metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles", meta = (ClampMin = "10.0", ClampMax = "1000.0", UIMin = "10.0", UIMax = "500.0"))
	float MaxDistanceMeters = 120.f;

	/** How often (seconds) the in-range / out-of-range set is recomputed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float RefreshIntervalSeconds = 0.5f;

	/**
	 * Hard cap on how many collectibles are outlined at once (nearest first). Protects both
	 * frame time and the game's finite outline mesh pool.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 MaxSimultaneousOutlines = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings HardDrivePods{ true, EOutlineColor::OC_ORANGE };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsBlue{ true, EOutlineColor::OC_BLUE };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsYellow{ true, EOutlineColor::OC_ORANGE };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsPurple{ true, EOutlineColor::OC_RED };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings MercerSpheres{ true, EOutlineColor::OC_GREEN };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Somersloops{ true, EOutlineColor::OC_RED };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BerylNut{ false, EOutlineColor::OC_GREEN };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Paleberry{ false, EOutlineColor::OC_BLUE };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BaconAgaric{ false, EOutlineColor::OC_ORANGE };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings DroppedItems{ false, EOutlineColor::OC_GREEN };

	/** Returns the settings block for a category. */
	const FVTCTypeSettings& GetFor(EVTCCollectibleCategory Category) const;

	/** Reads the live SML configuration into a struct. Safe to call every refresh tick. */
	static FVTCConfigStruct GetActiveConfig(const UObject* WorldContext);
};
