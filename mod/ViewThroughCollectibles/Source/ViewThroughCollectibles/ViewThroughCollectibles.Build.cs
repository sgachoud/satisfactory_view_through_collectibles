using UnrealBuildTool;

public class ViewThroughCollectibles : ModuleRules
{
	public ViewThroughCollectibles(ReadOnlyTargetRules Target) : base(Target)
	{
		// This small module compiles faster without the multi-gigabyte engine shared PCH.
		PCHUsage = PCHUsageMode.NoPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// FactoryGame's public headers expose types from these modules.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "DeveloperSettings", "PhysicsCore", "InputCore",
			"GeometryCollectionEngine", "AnimGraphRuntime", "AssetRegistry", "NavigationSystem",
			"AIModule", "GameplayTasks", "SlateCore", "Slate", "UMG", "RenderCore",
			"CinematicCamera", "Foliage", "NetCore", "GameplayTags", "Json", "JsonUtilities",
			"DummyHeaders", "FactoryGame", "SML"
		});
		if (Target.Type == TargetRules.TargetType.Editor)
		{
			PublicDependencyModuleNames.Add("AnimGraph");
		}
	}
}
