// Source/DawnOfTitans/DawnOfTitans.Build.cs
using UnrealBuildTool;

public class DawnOfTitans : ModuleRules
{
	public DawnOfTitans(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"DoTSim",
			"DoTKingdom"
		});

		// FDisplayMetrics: safe-area insets (notch, home indicator) for the HUD on phones.
		PrivateDependencyModuleNames.Add("ApplicationCore");
	}
}
