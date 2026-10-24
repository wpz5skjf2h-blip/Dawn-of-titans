// Source/DawnOfTitansEditor.Target.cs
using UnrealBuildTool;

public class DawnOfTitansEditorTarget : TargetRules
{
	public DawnOfTitansEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "DawnOfTitans" });
	}
}
