using UnrealBuildTool;

public class VRToyBox : ModuleRules
{
	public VRToyBox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HeadMountedDisplay：MotionControllerComponent 和 HMD 函数库都在这里
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay"
		});
	}
}
