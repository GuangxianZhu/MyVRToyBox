using UnrealBuildTool;

public class VRToyBoxEditorTarget : TargetRules
{
	public VRToyBoxEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VRToyBox");
	}
}
