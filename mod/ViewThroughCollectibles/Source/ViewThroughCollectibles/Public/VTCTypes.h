#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"   // FVector_NetQuantize100
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

/**
 * One collectible the server tells a client about (via UVTCCollectibleFeedComponent) so the
 * client can draw a see-through marker for collectibles too far away to have streamed in.
 * Only exists in multiplayer when the server also has the mod.
 */
USTRUCT()
struct FVTCFedCollectible
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FVector_NetQuantize100 Location = FVector::ZeroVector;

	/** EVTCCollectibleCategory as a byte, for a compact wire size. */
	UPROPERTY()
	uint8 Category = 0;

	bool operator==(const FVTCFedCollectible& Other) const
	{
		return Id == Other.Id && Location == Other.Location && Category == Other.Category;
	}
};

/** Effective client settings relevant to discovery. Appearance stays on the client. */
USTRUCT()
struct FVTCFeedPreferences
{
	GENERATED_BODY()

	UPROPERTY()
	double MaxDistanceCm = 0.0;
	UPROPERTY()
	int32 MaxEntries = 0;
	UPROPERTY()
	int32 EnabledCategories = 0;
	UPROPERTY()
	float RefreshSeconds = 0.4f;

	bool IsValid() const
	{
		constexpr int32 ValidMask = (1 << static_cast<uint8>(EVTCCollectibleCategory::MAX)) - 1;
		return FMath::IsFinite(MaxDistanceCm) && MaxDistanceCm >= 0.0 &&
			FMath::IsFinite(MaxDistanceCm * MaxDistanceCm) && MaxEntries >= 0 &&
			FMath::IsFinite(RefreshSeconds) && RefreshSeconds >= 0.1f &&
			(EnabledCategories & ~ValidMask) == 0;
	}
	bool Includes(uint8 Category) const
	{
		return Category < static_cast<uint8>(EVTCCollectibleCategory::MAX) &&
			(EnabledCategories & (1 << Category)) != 0;
	}
	bool operator==(const FVTCFeedPreferences& Other) const
	{
		return MaxDistanceCm == Other.MaxDistanceCm && MaxEntries == Other.MaxEntries &&
			EnabledCategories == Other.EnabledCategories && RefreshSeconds == Other.RefreshSeconds;
	}
};

/** A response is tagged with its request so old settings never win a race. */
USTRUCT()
struct FVTCFeedSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	uint32 Revision = 0;
	UPROPERTY()
	TArray<FVTCFedCollectible> Entries;
};

/** At most 128 entries are replicated at once; the owner acknowledges each page. */
USTRUCT()
struct FVTCFeedPage
{
	GENERATED_BODY()
	UPROPERTY()
	uint32 Revision = 0;
	UPROPERTY()
	uint32 Serial = 0;
	UPROPERTY()
	int32 Index = 0;
	UPROPERTY()
	bool bLast = false;
	UPROPERTY()
	TArray<FVTCFedCollectible> Entries;
};
