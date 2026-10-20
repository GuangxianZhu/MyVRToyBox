#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyCube.generated.h"

class AToyHand;
class UArrowComponent;
class UToyShapeComponent;

namespace ToyBox
{
	/** 控制台变量 toy.ShowGrabPoints：1 = 游戏里显示抓点箭头。 */
	bool ShowGrabPoints();
}

/**
 * 可以抓的 10cm 方块。
 *
 * 六个面中心各有一个抓点（GrabPoint_0..5，用箭头组件表示，编辑器里看得见）：
 *   X 轴 = 该面的外法线（从方块指向手），Z 轴 = 一个切线方向。
 * 手上也有一个同样约定的抓点（AToyHand::GripPoint）。
 * 抓起时选出最匹配的抓点，再把方块摆到"两个抓点完全重合"的位置——这就是手掌贴面的对齐。
 */
UCLASS()
class VRTOYBOX_API AToyCube : public AActor
{
	GENERATED_BODY()

public:
	AToyCube();

	static constexpr float SizeCm = 10.f;

	/**
	 * 给定手抓点的世界变换，返回"方块相对于手抓点"应该在的变换（缩放为 1）。
	 * 选点规则：面朝向最接近手掌的那个面，再绕法线把 4 种 90° 摆法里最贴合手指方向的挑出来。
	 */
	FTransform ComputeHoldTransform(const FTransform& GripWorld) const;

	void OnGrabbed(AToyHand* Hand);
	void OnReleased(const FVector& Velocity);
	AToyHand* GetHolder() const;

	void SetBaseColor(const FLinearColor& InColor);

	/** 手靠近时变亮。两只手可能同时悬停，所以用计数。 */
	void AddHover();
	void RemoveHover();

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TArray<TObjectPtr<UArrowComponent>> GrabPoints;

protected:
	virtual void BeginPlay() override;

private:
	void RefreshColor();

	TWeakObjectPtr<AToyHand> Holder;
	FLinearColor BaseColor = FLinearColor(0.9f, 0.5f, 0.2f);
	int32 HoverCount = 0;
};
