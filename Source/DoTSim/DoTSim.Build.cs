// Source/DoTSim/DoTSim.Build.cs
// Deterministic battle simulation. Pure C++: the sources never include engine headers
// (except DoTSimModule.cpp), so the same files also build standalone (SimTools/CMakeLists.txt)
// for tests, the balance harness and the server-side battle verifier.
using UnrealBuildTool;

public class DoTSim : ModuleRules
{
	public DoTSim(ReadOnlyTargetRules Target) : base(Target)
	{
		// No shared PCH: keeps engine macros out of the simulation sources.
		PCHUsage = PCHUsageMode.NoPCHs;
		// Every .cpp is compiled on its own, exactly like the standalone build.
		bUseUnity = false;
		bEnableExceptions = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
