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

	/** Adds category-coloured map dots; only highlighted dots appear on the compass. */
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

	// Effective settings shared by local selection and server queries. Reject non-finite
	// values, clamp negatives, and keep the minimum refresh at 0.1 s. No hidden upper cap.
	float SafeRefreshIntervalSeconds() const { return FMath::IsFinite(RefreshIntervalSeconds) ? FMath::Max(0.1f, RefreshIntervalSeconds) : 0.4f; }
	double MaxDistanceCm() const { return FMath::IsFinite(MaxDistanceMeters) ? FMath::Max(0.f, MaxDistanceMeters) * 100.0 : 0.0; }
	int32 SafeMaxSimultaneous() const { return FMath::Max(0, MaxSimultaneousOutlines); }
	float SafeThickness() const { return FMath::IsFinite(OutlineThicknessPixels) ? FMath::Max(1.f, OutlineThicknessPixels) : 2.f; }
	float SafeFillOpacity() const { return FMath::IsFinite(OccludedFillOpacity) ? FMath::Clamp(OccludedFillOpacity, 0.f, 1.f) : 0.12f; }
	FVTCFeedPreferences FeedPreferences() const;

	/** Reads the live SML configuration into a struct. Safe to call every refresh tick. */
	static FVTCConfigStruct GetActiveConfig(const UObject* WorldContext);
};
