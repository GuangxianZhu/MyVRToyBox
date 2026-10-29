#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyArticulated.generated.h"

class AToyHand;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** 柜门转、抽屉拉。 */
UENUM()
enum class EToyJointType : uint8
{
	/** 绕铰链轴转（柜门），Value 是角度（度） */
	Hinge,
	/** 沿轴平移（抽屉），Value 是拉出来的距离（厘米） */
	Slide
};

/**
 * 家具上能开合的一块：柜门（绕铰链转，0~MaxValue 度）、抽屉（沿轴拉出，0~MaxValue 厘米）。
 *
 * 由样板间生成脚本（Tools/archviz/build_studio_archviz.py）把柜子上单独的门板 / 抽屉网格换成它：网格、材质、位置照搬，
 * 关上的位置就是关卡里原来的位置（Value = 0）。不做物理：运动学地跟着手转 / 拉，松手就停在那里。
 * 抓法：捏住门板 / 抽屉面板任何地方往外拉（手不会把它挂到手上，见 IToyGrabbable::IsConstrained）；触屏「摆件复位」把它关上。
 */
UCLASS()
class VRTOYBOX_API AToyArticulated : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyArticulated();

	/** 关上（Value = 0）。 */
	void ResetToHome();
	bool IsAwayFromHome() const { return FMath::Abs(Value) > 0.5f; }
	float GetValue() const { return Value; }
	void SetValue(float NewValue);

	virtual bool IsConstrained() const override { return true; }
	virtual void UpdateConstrainedGrab(const FVector& HandLocation) override;
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override;
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual AToyHand* GetHolder() const override;
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override { return EToyGripStyle::Pinch; }
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;
	virtual FString GetMaterialText() const override { return MaterialText; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	EToyJointType JointType = EToyJointType::Hinge;

	/** 关上时（关卡里放的位置）的轴：铰链 = 转轴方向（按右手定则正转是打开），抽屉 = 拉出来的方向。Actor 坐标系，单位向量。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FVector Axis = FVector::UpVector;

	/** 铰链轴上的一点（Actor 坐标系，厘米）。抽屉不用。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FVector Pivot = FVector::ZeroVector;

	/** 最多开多大：柜门 100°，抽屉一般是柜子深度的七成。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MaxValue = 100.f;

	/** 信息牌标题，例如"餐边柜的门"。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString DisplayName;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString MaterialText = TEXT("木");

protected:
	virtual void BeginPlay() override;

private:
	void RefreshOverlay();
	/** 手在这一刻对应的"开度"：铰链 = 手绕轴的角度，抽屉 = 手沿轴的位置 */
	float HandParameter(const FVector& HandLocation) const;

	FTransform HomeTransform;
	float Value = 0.f;
	float ValueAtGrab = 0.f;
	float HandAtGrab = 0.f;
	TWeakObjectPtr<AToyHand> Holder;
	int32 HoverCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoverMaterial;
};
