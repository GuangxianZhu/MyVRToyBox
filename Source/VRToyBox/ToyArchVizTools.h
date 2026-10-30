#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ToyArchVizTools.generated.h"

class UStaticMesh;
class UWorldSubsystem;

/**
 * 样板间生成脚本（Tools/archviz/build_studio_archviz.py）用的编辑器小工具，只在编辑器里有用。
 * 命令行（-run=pythonscript）模式下 StaticMeshEditorSubsystem 拿不到，加碰撞只能自己写。
 */
UCLASS()
class VRTOYBOX_API UToyArchVizTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 摆件要做物理模拟，网格得有简单碰撞。已经有就不动；没有就加一个：bBox = 包围盒（书），否则 = 整个网格顶点的凸包（瓶子、花盆）。
	 * 返回一句说明（写进日志）。
	 */
	UFUNCTION(BlueprintCallable, Category = "ToyBox|ArchViz")
	static FString EnsurePropCollision(UStaticMesh* Mesh, bool bBox);

	/** 网格有几个简单碰撞形状（盒子 + 球 + 胶囊 + 凸包）。 */
	UFUNCTION(BlueprintCallable, Category = "ToyBox|ArchViz")
	static int32 GetSimpleCollisionCount(UStaticMesh* Mesh);

	/** 家具、墙：物理模拟直接用网格三角面当碰撞（"复杂碰撞当简单碰撞"），扔出去的摆件才不会穿过去。已经是了返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "ToyBox|ArchViz")
	static bool UseComplexAsSimple(UStaticMesh* Mesh);

	/**
	 * 编辑器世界的 GPU Lightmass 子系统（UGPULightmassSubsystem：Launch / IsRunning / GetPercentage），没开插件就是空。
	 * Python 里拿不到世界子系统（没有 SubsystemBlueprintLibrary），按类名找，工程也不用依赖 GPULightmass 模块。
	 */
	UFUNCTION(BlueprintCallable, Category = "ToyBox|ArchViz", meta = (WorldContext = "WorldContext"))
	static UWorldSubsystem* GetGPULightmassSubsystem(UObject* WorldContext);
};
