using UnrealBuildTool;

// This mirrors what Alpakit's "C++ & Blueprint" template generates, trimmed to what this
// mod actually uses. When you scaffold the plugin with Alpakit, you can keep the generated
// Build.cs as-is (it's a superset) and just drop the VTC*.h/.cpp files in next to it.
public class ViewThroughCollectibles : ModuleRules
{
	public ViewThroughCollectibles(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",          // UWorldSubsystem, APostProcessVolume, MPC, MID, KismetMaterialLibrary
			"DeveloperSettings",
			"RenderCore",
			"SlateCore",
			"DummyHeaders",    // FactoryGame header stubs
			"FactoryGame",     // AFGScannableSubsystem, AFGItemPickup(_Spawnable), AFGDropPod, UFGItemDescriptor
			"SML",             // UConfigManager (mod configuration)
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
