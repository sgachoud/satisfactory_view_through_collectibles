#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VTCCategoryTables.h"
#include "VTCServerFeedSubsystem.generated.h"

/** Optional authority service. Each remote client supplies its own discovery settings. */
UCLASS()
class UVTCServerFeedSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
private:
	void RefreshFeeds();
	FVTCCategoryTables Tables;
	FTimerHandle RefreshTimerHandle;
};