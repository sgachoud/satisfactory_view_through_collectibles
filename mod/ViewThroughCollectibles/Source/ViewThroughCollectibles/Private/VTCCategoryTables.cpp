#include "VTCCategoryTables.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ReflectedTypeAccessors.h"   // StaticEnum<>()
#include "GameFramework/Actor.h"

#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "FGDropPod.h"
#include "Resources/FGItemDescriptor.h"

void FVTCCategoryTables::Load()
{
	ByDescriptor.Reset();
	ByActorClass.Reset();

	auto Desc = [this](const TCHAR* Path, EVTCCollectibleCategory Cat)
	{
		ByDescriptor.Add(FSoftClassPath(Path), Cat);
	};
	auto Cls = [this](const TCHAR* Path, EVTCCollectibleCategory Cat)
	{
		ByActorClass.Add(FSoftClassPath(Path), Cat);
	};

	// Verified paths - see docs/FINDINGS.md.
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal.Desc_Crystal_C"), EVTCCollectibleCategory::PowerSlugMk1);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal_mk2.Desc_Crystal_mk2_C"), EVTCCollectibleCategory::PowerSlugMk2);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/Desc_Crystal_mk3.Desc_Crystal_mk3_C"), EVTCCollectibleCategory::PowerSlugMk3);
	// WAT1 is the Somersloop, WAT2 is the Mercer Sphere (verified in-game — the folder numbering
	// is the reverse of what you'd expect).
	Desc(TEXT("/Game/FactoryGame/Prototype/WAT/Desc_WAT1.Desc_WAT1_C"), EVTCCollectibleCategory::Somersloop);
	Desc(TEXT("/Game/FactoryGame/Prototype/WAT/Desc_WAT2.Desc_WAT2_C"), EVTCCollectibleCategory::MercerSphere);
	// Folder names don't match in-game names: Desc_Berry = Beryl Nut, Desc_Nut = Paleberry,
	// Desc_Shroom = Bacon Agaric. TODO verify in-game before shipping.
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Berry/Desc_Berry.Desc_Berry_C"), EVTCCollectibleCategory::BerylNut);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/Nut/Desc_Nut.Desc_Nut_C"), EVTCCollectibleCategory::Paleberry);
	Desc(TEXT("/Game/FactoryGame/Resource/Environment/DesertShroom/Desc_Shroom.Desc_Shroom_C"), EVTCCollectibleCategory::BaconAgaric);

	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal.BP_Crystal_C"), EVTCCollectibleCategory::PowerSlugMk1);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk2.BP_Crystal_mk2_C"), EVTCCollectibleCategory::PowerSlugMk2);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal_mk3.BP_Crystal_mk3_C"), EVTCCollectibleCategory::PowerSlugMk3);
	Cls(TEXT("/Game/FactoryGame/Prototype/WAT/BP_WAT1.BP_WAT1_C"), EVTCCollectibleCategory::Somersloop);
	Cls(TEXT("/Game/FactoryGame/Prototype/WAT/BP_WAT2.BP_WAT2_C"), EVTCCollectibleCategory::MercerSphere);
	// Flora pickup BP classes — needed for the server marker feed, where only the class is
	// known (no streamed actor, no descriptor). TODO verify these paths in-game.
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Berry/BP_Berry.BP_Berry_C"), EVTCCollectibleCategory::BerylNut);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/Nut/BP_Nut.BP_Nut_C"), EVTCCollectibleCategory::Paleberry);
	Cls(TEXT("/Game/FactoryGame/Resource/Environment/DesertShroom/BP_Shroom.BP_Shroom_C"), EVTCCollectibleCategory::BaconAgaric);

	// Ini overrides (Game.ini): add/replace without recompiling.
	//   [ViewThroughCollectibles.Categories]
	//   +Descriptor=(Path="/Game/.../Desc_Foo.Desc_Foo_C", Category="Somersloop")
	//   +ActorClass=(Path="/Game/.../BP_Foo.BP_Foo_C", Category="Somersloop")
	auto ApplyOverrides = [](const TCHAR* Key, TMap<FSoftClassPath, EVTCCollectibleCategory>& Table)
	{
		TArray<FString> Entries;
		GConfig->GetArray(TEXT("ViewThroughCollectibles.Categories"), Key, Entries, GGameIni);
		for (const FString& Entry : Entries)
		{
			FString PathStr, CategoryStr;
			if (FParse::Value(*Entry, TEXT("Path="), PathStr) && FParse::Value(*Entry, TEXT("Category="), CategoryStr))
			{
				const int64 Val = StaticEnum<EVTCCollectibleCategory>()->GetValueByNameString(CategoryStr);
				if (Val >= 0 && Val < static_cast<int64>(EVTCCollectibleCategory::MAX))
				{
					Table.Add(FSoftClassPath(PathStr), static_cast<EVTCCollectibleCategory>(Val));
				}
			}
		}
	};
	ApplyOverrides(TEXT("Descriptor"), ByDescriptor);
	ApplyOverrides(TEXT("ActorClass"), ByActorClass);
}

bool FVTCCategoryTables::Resolve(const AActor* Actor, const UClass* FallbackClass, EVTCCollectibleCategory& OutCategory) const
{
	if (Actor)
	{
		if (Actor->IsA(AFGDropPod::StaticClass()))
		{
			OutCategory = EVTCCollectibleCategory::HardDrivePod;
			return true;
		}
		if (Actor->IsA(AFGItemPickup_Spawnable::StaticClass()))
		{
			OutCategory = EVTCCollectibleCategory::DroppedItem;
			return true;
		}
		if (const AFGItemPickup* Pickup = Cast<AFGItemPickup>(Actor))
		{
			const TSubclassOf<UFGItemDescriptor> Desc = Pickup->GetPickupItemClass();
			if (*Desc)
			{
				if (const EVTCCollectibleCategory* Found = ByDescriptor.Find(FSoftClassPath(*Desc)))
				{
					OutCategory = *Found;
					return true;
				}
			}
		}
	}

	// Class-path fallback (used when the actor is not streamed in, or descriptor lookup missed).
	if (FallbackClass && FallbackClass->IsChildOf(AFGDropPod::StaticClass()))
	{
		OutCategory = EVTCCollectibleCategory::HardDrivePod;
		return true;
	}
	for (const UClass* C = Actor ? Actor->GetClass() : FallbackClass;
	     C && C != AActor::StaticClass(); C = C->GetSuperClass())
	{
		if (const EVTCCollectibleCategory* Found = ByActorClass.Find(FSoftClassPath(C)))
		{
			OutCategory = *Found;
			return true;
		}
	}
	return false;
}
