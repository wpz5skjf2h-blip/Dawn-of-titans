// Source/DoTKingdom/DoTKingdom.Build.cs
// Kingdom economy: city buildings, builders, timers, resources, expansions, lands, portal stones.
// Pure C++ like DoTSim (only DoTKingdomModule.cpp includes engine headers), so the same files build
// standalone (SimTools/CMakeLists.txt), in the browser demo (Web/build.sh) and on a game server.
using UnrealBuildTool;

public class DoTKingdom : ModuleRules
{
	public DoTKingdom(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "DoTSim" });
	}
}
