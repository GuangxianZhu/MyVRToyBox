#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyExplodedModel.generated.h"

class AToyPartUnit;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * 可以在"原始（装配）"和"爆炸"之间切换、还能把零件拿起来看的模型，数据来自一个 JSON 清单。
 *
 * 清单由 Tools/import_motor.py 在导入 glb 时生成（Content/Models/<名字>/parts.json）：
 *   parts[] = 网格资产路径 + 属于哪个抓取单元 + 爆炸步骤 [x, y, z, 出场时刻]；
 *   units[] = 抓取单元：名字 / 材料 / 作用（信息牌用）+ 抓法（圆柱面 / 包围盒的面 / 捏）。
 * 每个单元生成一个 AToyPartUnit 挂在 ModelRoot 上。所有零件原点都在模型轴心，
 * 爆炸 = 把每个单元移动它那几步位移之和；每一步在总进度 T 的 [出场时刻×0.6, 出场时刻×0.6+0.4] 区间里缓动走完，
 * 所以零件会按真实拆装顺序先后飞出（和 ITBasics 里 Blender 动画、网页版同一套公式）。
 *
 * 抓取：
 *   - 完全装配时：抓哪里都 = 抓整台电机（模型自己的 GrabBox），松手后飞回桌上的支架。
 *   - 完全爆炸后：每个单元单独抓（单元的 GrabBox），松手飞回爆炸位置。定子 + 机座是基座，不能抓。
 *   - 爆炸 / 收回动画进行中：都不能抓。
 */
UCLASS()
class VRTOYBOX_API AToyExplodedModel : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyExplodedModel();

	virtual void Tick(float DeltaSeconds) override;

	/** 读清单、生成抓取单元、按 TargetLengthCm 缩放并让模型底面落在 Actor 原点。找不到清单或网格时返回 false。 */
	bool LoadManifest(const FString& ManifestPath);

	void SetExploded(bool bInExploded);
	bool IsExploded() const { return bExploded; }

	/** 所有零件（和整机）松手、飞回原位。爆炸 / 装配状态不变。 */
	void ResetAll();

	/** 单元现在"应该在"的世界变换：当前爆炸进度下的位移 × ModelRoot。 */
	FTransform GetUnitHomeWorld(const AToyPartUnit* Unit) const;

	/** 单元飞回原位后调用：挂回 ModelRoot，位置重新交给爆炸进度管。 */
	void OnUnitHome(AToyPartUnit* Unit);

	/** 在单元的原位显示 / 隐藏半透明虚影。 */
	void ShowGhost(AToyPartUnit* Unit, bool bShow);

	UMaterialInterface* GetHoverMaterial() const;

	const TArray<TObjectPtr<AToyPartUnit>>& GetUnits() const { return Units; }

	// IToyGrabbable：装配状态下抓整台电机
	virtual bool CanGrab() const override;
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override;
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual AToyHand* GetHolder() const override;
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override { return EToyGripStyle::Palm; }
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;
	/** 整台机器拿起来：按铸铁 / 钢的声音算。 */
	virtual FString GetMaterialText() const override { return TEXT("钢"); }

	/** 装配状态下最长边缩放到这么长（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float TargetLengthCm = 50.f;  // 清单里 model.length_cm 有值时以清单为准

	/** 从完全装配到完全爆炸用的秒数。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ExplodeDuration = 2.5f;

	/** 整机松手后飞回支架用的秒数。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ReturnTime = 0.4f;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 单元都挂在这里；缩放也放在这里，所以爆炸位移会跟着模型一起缩放。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ModelRoot;

	/** 装配状态下抓整机用的检测盒（只查询，不参与物理）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> GrabBox;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyProgress();
	void UpdateGrabModes();
	void TickReturn(float DeltaSeconds);
	void RefreshOverlay();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AToyPartUnit>> Units;

	/** 每个单元的虚影网格（懒创建），和 Units 下标一一对应。 */
	TArray<TArray<TObjectPtr<UStaticMeshComponent>>> Ghosts;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoverMaterial;

	FText ModelTitle;
	FText ModelBody;

	bool bExploded = false;
	float Progress = 0.f;

	/** -1 = 还没决定；0 = 整机可抓；1 = 零件可抓；2 = 都不能抓（动画中） */
	int32 GrabMode = -1;

	TWeakObjectPtr<AToyHand> Holder;
	int32 HoverCount = 0;

	/** 整机被抓之前挂在哪（桌上的 MotorMount），松手飞回这里。 */
	TWeakObjectPtr<USceneComponent> HomeParent;
	bool bReturning = false;
	float ReturnAlpha = 0.f;
	FTransform ReturnFrom;
};
