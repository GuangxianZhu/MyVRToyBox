using UnrealBuildTool;

public class VRToyBox : ModuleRules
{
	public VRToyBox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// HeadMountedDisplay：MotionControllerComponent 在这里
		// Json：读爆炸图零件清单
		// UMG / Slate：手边的零件信息牌（WidgetComponent + Slate 文字，能显示中文）
		// XRBase：HMD 函数库（UHeadMountedDisplayFunctionLibrary）在新版引擎里搬进了 XRBase 插件
		// ProceduralMeshComponent：传送抛物线的细管、落点圈
		// RHI：toy.GpuTime 读 GPU 帧时间
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "XRBase", "Json", "UMG", "Slate", "SlateCore",
			"ProceduralMeshComponent", "RHI"
		});

		// 样板间生成脚本给摆件加凸包碰撞要读网格顶点（ToyArchVizTools，只在编辑器里）
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription" });
		}
	}
}
