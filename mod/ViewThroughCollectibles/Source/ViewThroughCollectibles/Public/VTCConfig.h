#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.h"
#include "VTCConfig.generated.h"

/**
 * Mirror of the mod's SML configuration.
 *
 * SML's in-editor "Configuration Tool" generates a UModConfiguration asset and, on request,
 * a matching C++ struct + GetActiveConfig() accessor from a struct of this exact shape. We
 * keep a hand-written version here so the C++ side compiles and is reviewable without the
 * editor. When you generate the header from the asset, keep the field names identical to
 * these (the subsystem reads them by name) or update the subsystem to match.
 *
 * See docs/CONTENT-ASSETS.md for the asset build steps.
 */
USTRUCT(BlueprintType)
struct VIEWTHROUGHCOLLECTIBLES_API FVTCConfigStruct
{
	GENERATED_BODY()

	/** Collectibles farther than this from the local player are not outlined. Metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (ClampMin = "10.0", ClampMax = "2000.0", UIMin = "10.0", UIMax = "500.0"))
	float MaxDistanceMeters = 120.f;

	/** How often (seconds) the in-range / out-of-range set is recomputed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float RefreshIntervalSeconds = 0.4f;

	/**
	 * Hard cap on how many collectibles are outlined at once (nearest first). Protects frame
	 * time in collectible-dense areas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (ClampMin = "1", ClampMax = "2048"))
	int32 MaxSimultaneousOutlines = 200;

	/** Outline edge width in screen pixels (at 1080p; scaled for other resolutions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float OutlineThicknessPixels = 2.f;

	/**
	 * How strongly the silhouette is tinted where the collectible is hidden behind geometry
	 * (0 = crisp edge only, 1 = solid fill). Multiplied by each colour's alpha.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedFillOpacity = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings HardDrivePods{ true, FLinearColor(1.00f, 0.45f, 0.05f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsBlue{ true, FLinearColor(0.10f, 0.55f, 1.00f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsYellow{ true, FLinearColor(1.00f, 0.85f, 0.10f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings PowerSlugsPurple{ true, FLinearColor(0.65f, 0.20f, 1.00f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings MercerSpheres{ true, FLinearColor(0.10f, 1.00f, 0.55f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Somersloops{ true, FLinearColor(1.00f, 0.10f, 0.35f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BerylNut{ false, FLinearColor(0.55f, 0.85f, 0.20f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings Paleberry{ false, FLinearColor(0.60f, 0.80f, 0.95f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings BaconAgaric{ false, FLinearColor(0.90f, 0.55f, 0.35f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Types")
	FVTCTypeSettings DroppedItems{ false, FLinearColor(0.95f, 0.95f, 0.95f, 1.f) };

	/** Returns the settings block for a category. */
	const FVTCTypeSettings& GetFor(EVTCCollectibleCategory Category) const;

	/** Reads the live SML configuration into a struct. Safe to call every refresh tick. */
	static FVTCConfigStruct GetActiveConfig(const UObject* WorldContext);
};
