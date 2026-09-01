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
			"Engine",
			"DeveloperSettings",
			"SML",
			"FactoryGame",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
