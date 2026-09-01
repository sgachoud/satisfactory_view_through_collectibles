#pragma once

#include "CoreMinimal.h"
#include "VTCTypes.generated.h"

/**
 * Every collectible family the mod can outline. Kept flat (one entry per user-facing toggle)
 * so it maps 1:1 to the config UI and to FVTCConfigStruct's per-type fields.
 *
 * The numeric values double as the Custom Depth stencil value written for that category
 * (offset by VTC::StencilBase in the subsystem), and as the MPC colour parameter index, so
 * keep them contiguous starting at 0 and keep MAX last.
 */
UENUM(BlueprintType)
enum class EVTCCollectibleCategory : uint8
{
	HardDrivePod    = 0  UMETA(DisplayName = "Hard Drive Drop Pods"),
	PowerSlugMk1    = 1  UMETA(DisplayName = "Power Slugs (Blue)"),
	PowerSlugMk2    = 2  UMETA(DisplayName = "Power Slugs (Yellow)"),
	PowerSlugMk3    = 3  UMETA(DisplayName = "Power Slugs (Purple)"),
	MercerSphere    = 4  UMETA(DisplayName = "Mercer Spheres"),
	Somersloop      = 5  UMETA(DisplayName = "Somersloops"),
	BerylNut        = 6  UMETA(DisplayName = "Beryl Nut"),
	Paleberry       = 7  UMETA(DisplayName = "Paleberry"),
	BaconAgaric     = 8  UMETA(DisplayName = "Bacon Agaric"),
	DroppedItem     = 9  UMETA(DisplayName = "Dropped Items"),

	MAX             = 10 UMETA(Hidden)
};

/**
 * Per-category settings block. Field names match the keys inside each Section of the SML
 * ModConfiguration (`Enabled`, `Color`) so UConfigManager::FillConfigurationStruct binds
 * them by reflection.
 */
USTRUCT(BlueprintType)
struct FVTCTypeSettings
{
	GENERATED_BODY()

	FVTCTypeSettings() = default;
	FVTCTypeSettings(bool bInEnabled, const FString& InColorHex)
		: Enabled(bInEnabled), Color(InColorHex) {}

	/** Whether this collectible family is outlined at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles")
	bool Enabled = true;

	/** Outline colour as a hex string, "RRGGBB" or "RRGGBBAA" (with or without a leading #). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles")
	FString Color = TEXT("FFFFFF");

	/** Parsed colour. Falls back to white on an unparseable string. Alpha defaults to 1. */
	FLinearColor GetLinearColor() const;
};
