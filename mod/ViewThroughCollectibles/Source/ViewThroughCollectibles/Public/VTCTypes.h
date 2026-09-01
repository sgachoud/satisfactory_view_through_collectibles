#pragma once

#include "CoreMinimal.h"
// EOutlineColor lives in FactoryGame. Header name has been stable across SML releases, but
// TODO(needs FactoryGame source access): confirm the path and the enumerator spelling
// (OC_RED / OC_ORANGE / ...) against the version you build against.
#include "FGOutlineComponent.h"
#include "VTCTypes.generated.h"

/**
 * Every collectible family the mod can outline. Kept flat (one entry per user-facing toggle)
 * so it maps 1:1 to the config UI and to FVTCConfigStruct's per-type fields.
 */
UENUM(BlueprintType)
enum class EVTCCollectibleCategory : uint8
{
	HardDrivePod    UMETA(DisplayName = "Hard Drive Drop Pods"),
	PowerSlugMk1    UMETA(DisplayName = "Power Slugs (Blue)"),
	PowerSlugMk2    UMETA(DisplayName = "Power Slugs (Yellow)"),
	PowerSlugMk3    UMETA(DisplayName = "Power Slugs (Purple)"),
	MercerSphere    UMETA(DisplayName = "Mercer Spheres"),
	Somersloop      UMETA(DisplayName = "Somersloops"),
	BerylNut        UMETA(DisplayName = "Beryl Nut"),
	Paleberry       UMETA(DisplayName = "Paleberry"),
	BaconAgaric     UMETA(DisplayName = "Bacon Agaric"),
	DroppedItem     UMETA(DisplayName = "Dropped Items"),

	MAX             UMETA(Hidden)
};

/** Per-category settings block shown in the mod config menu. */
USTRUCT(BlueprintType)
struct FVTCTypeSettings
{
	GENERATED_BODY()

	FVTCTypeSettings() = default;
	FVTCTypeSettings(bool bInEnabled, EOutlineColor InColor)
		: bEnabled(bInEnabled), Color(InColor) {}

	/** Whether this collectible family is outlined at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles")
	bool bEnabled = true;

	/**
	 * Outline colour, chosen from the game's built-in outline palette.
	 *
	 * The game's UFGOutlineComponent only supports this fixed set of named colours, not an
	 * arbitrary RGB value. If a custom-colour entry point is found on the outline component
	 * in a future FactoryGame source drop, this can be widened to an FLinearColor without
	 * changing the rest of the mod (only VTCOutlineSubsystem::ApplyOutline needs to adapt).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Through Collectibles")
	EOutlineColor Color = EOutlineColor::OC_RED;
};
