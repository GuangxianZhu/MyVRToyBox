#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyProp.generated.h"

class AToyHand;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 场景里能拿起来的摆件（花瓶、书、杯子、小盆栽……）。
 *
 * 由样板间生成脚本（Tools/archviz/build_studio_archviz.py）把原来的静态摆件换成它：网格、材质、位置照搬，
 * 改成可移动 + 物理模拟（开局睡着，碰到才醒，叠着放的书不会自己乱跳）。
 * 抓法和方块一样：自由捏取，松手恢复物理、按手的速度扔出去。触屏「摆件复位」把所有摆件放回开局的位置。
 */
UCLASS()
class VRTOYBOX_API AToyProp : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyProp();

	/** 放回开局的位置和朝向，速度清零。 */
	void ResetToHome();

	/** 离开局的位置远不远（超过 2cm 或转了 5° 以上）。触屏状态栏数"不在原位"的件数用。 */
	bool IsAwayFromHome() const;

	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override;
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual AToyHand* GetHolder() const override;
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override;
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;
	virtual FString GetMaterialText() const override { return MaterialText; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 信息牌标题，例如"玻璃花瓶"。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString DisplayName;

	/** 材料，例如"玻璃""陶瓷""纸"：信息牌第二行显示，声音和震动也按它分（UToyFeedback::ClassifyMaterial）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString MaterialText;

	/** 质量（千克）。0 = 按包围盒体积粗估。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MassKg = 0.f;

protected:
	virtual void BeginPlay() override;

private:
	void RefreshOverlay();

	FTransform HomeTransform;
	TWeakObjectPtr<AToyHand> Holder;
	int32 HoverCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoverMaterial;
};
