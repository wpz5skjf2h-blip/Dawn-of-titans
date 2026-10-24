// Source/DawnOfTitans.Target.cs
using UnrealBuildTool;

public class DawnOfTitansTarget : TargetRules
{
	public DawnOfTitansTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "DawnOfTitans" });
	}
}
