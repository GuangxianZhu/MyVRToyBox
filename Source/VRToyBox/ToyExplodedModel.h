#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyExplodedModel.generated.h"

class UStaticMeshComponent;

/**
 * 可以在"原始（装配）"和"爆炸"之间切换的模型，零件数据来自一个 JSON 清单。
 *
 * 清单由 Tools/import_motor.py 在导入 glb 时生成（Content/Models/<名字>/parts.json）：
 *   每个零件 = 一个静态网格资产路径 + 一串爆炸步骤 [x, y, z, 出场时刻]。
 * 所有零件的原点都在模型轴心，装配状态下零件相对位置都是 0；爆炸 = 把每个零件移动它那几步位移之和。
 * 每一步在总进度 T 的 [出场时刻×0.6, 出场时刻×0.6+0.4] 区间里缓动走完，
 * 所以零件会按真实拆装顺序先后飞出；收回时 T 倒着走，顺序自然反过来。
 * （和 ITBasics 里 Blender 动画、网页版用的是同一套公式。）
 */
UCLASS()
class VRTOYBOX_API AToyExplodedModel : public AActor
{
	GENERATED_BODY()

public:
	AToyExplodedModel();

	virtual void Tick(float DeltaSeconds) override;

	/** 读清单、加载网格、按 TargetLengthCm 缩放并让模型底面落在 Actor 原点。找不到清单或网格时返回 false。 */
	bool LoadManifest(const FString& ManifestPath);

	void SetExploded(bool bInExploded);
	bool IsExploded() const { return bExploded; }

	/** 装配状态下最长边缩放到这么长（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float TargetLengthCm = 50.f;

	/** 从完全装配到完全爆炸用的秒数。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ExplodeDuration = 2.5f;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 零件都挂在这里；缩放也放在这里，所以爆炸位移会跟着模型一起缩放。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ModelRoot;

private:
	struct FExplodeStep
	{
		FVector Offset;
		float Start;
	};

	void ApplyProgress();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	/** 和 Parts 一一对应。 */
	TArray<TArray<FExplodeStep>> PartSteps;

	bool bExploded = false;
	float Progress = 0.f;
};
