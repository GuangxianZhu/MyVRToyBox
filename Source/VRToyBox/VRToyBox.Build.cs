using UnrealBuildTool;

public class VRToyBox : ModuleRules
{
	public VRToyBox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HeadMountedDisplay：MotionControllerComponent 在这里
		// Json：读爆炸图零件清单
		// XRBase：HMD 函数库（UHeadMountedDisplayFunctionLibrary）在新版引擎里搬进了 XRBase 插件
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "XRBase", "Json"
		});
	}
}
