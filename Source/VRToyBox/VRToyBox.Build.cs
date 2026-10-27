using UnrealBuildTool;

public class VRToyBox : ModuleRules
{
	public VRToyBox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// 每个 .cpp 单独编译：好几个文件的匿名命名空间里有同名的小工具（Font、BackColor 这些），合并编译（unity）会撞名
		bUseUnity = false;

		// HeadMountedDisplay：MotionControllerComponent 在这里
		// Json：读爆炸图零件清单
		// UMG / Slate：手边的零件信息牌（WidgetComponent + Slate 文字，能显示中文）
		// XRBase：HMD 函数库（UHeadMountedDisplayFunctionLibrary）在新版引擎里搬进了 XRBase 插件
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "XRBase", "Json", "UMG", "Slate", "SlateCore"
		});
	}
}
