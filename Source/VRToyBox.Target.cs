using UnrealBuildTool;

public class VRToyBoxTarget : TargetRules
{
	public VRToyBoxTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VRToyBox");
	}
}
