using UnrealBuildTool;

public class ViewThroughCollectibles : ModuleRules
{
	public ViewThroughCollectibles(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",          // UWorldSubsystem, APostProcessVolume, MPC, MID, KismetMaterialLibrary
			"SML",             // UConfigManager (mod configuration)
			"FactoryGame",     // AFGScannableSubsystem, AFGItemPickup(_Spawnable), AFGDropPod, UFGItemDescriptor
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
