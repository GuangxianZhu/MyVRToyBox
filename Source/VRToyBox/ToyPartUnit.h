#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyPartUnit.generated.h"

class AToyExplodedModel;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** 爆炸路线的一步：位移（模型坐标，厘米，未缩放）+ 出场时刻（0~1）。 */
struct FToyExplodeStep
{
	FVector Offset;
	float Start;
};

/** 零件的抓法，对应 parts.json 里 units[].grab.type。 */
enum class EToyPartGrab : uint8
{
	/** 不能抓（定子 + 机座这个基座） */
	None,
	/** 绕电机轴的回转体：手贴在一段圆柱面上，位置和角度取离手最近的那一点 */
	Cylinder,
	/** 方块一样：手掌贴包围盒的一个面 */
	Box,
	/** 小件：零件中心对到拇指和食指之间，保持抓起瞬间的朝向 */
	Pinch
};

/** 一段抓取圆柱：沿电机轴（模型 X）从 X0 到 X1，半径 Radius（厘米，未缩放）。 */
struct FToyCylinderSegment
{
	float X0;
	float X1;
	float Radius;
};

/**
 * 一个"抓取单元"：一起拆下来的一组零件（比如转子总成 = 轴 + 铁芯 + 28 根导条 + 端环 + 两个轴承），
 * 或者一颗单独的螺钉。由 AToyExplodedModel 按 parts.json 生成，平时挂在模型的 ModelRoot 上，
 * 位置由模型的爆炸进度决定。
 *
 * Actor 原点 = 模型原点（电机轴心），零件网格的顶点本来就是模型坐标，所以网格都以单位变换挂在 Root 上。
 *
 * 状态：在家（挂在模型上） → 被抓（挂在手上） → 松手后飞回家（0.4 秒；在磁吸范围内 0.12 秒） → 在家。
 * 拿着靠近原位 MagnetDistance 以内时，模型在原位显示一个半透明虚影。
 */
UCLASS()
class VRTOYBOX_API AToyPartUnit : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyPartUnit();

	virtual void Tick(float DeltaSeconds) override;

	/** 模型生成单元后依次调用：Setup → AddMesh（若干次）→ 模型定好缩放后 FinishSetup。 */
	void Setup(AToyExplodedModel* InModel, const FString& InId, const FText& InTitle, const FText& InBody,
		EToyPartGrab InGrab, TArray<FToyCylinderSegment> InSegments, const FBox& InGrabBox);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FString& PartName);
	void SetExplodeSteps(const TArray<FToyExplodeStep>& InSteps);
	void FinishSetup();

	/** 爆炸进度为 T 时，这个单元相对模型的位移（模型坐标，未缩放）。 */
	FVector GetExplodeOffset(float T) const;

	/** 在家 = 没被抓、也不在飞回去的路上，位置归模型管。 */
	bool IsHome() const;
	bool IsGrabbableUnit() const { return Grab != EToyPartGrab::None; }

	/** 模型在爆炸 / 装配状态之间切换时打开 / 关闭单元的检测盒。 */
	void SetGrabEnabled(bool bEnabled);

	/** 模型高亮整机时，也要给每个单元的网格叠加高亮。 */
	void SetOverlay(UMaterialInterface* Overlay);

	const FString& GetUnitId() const { return Id; }
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetMeshes() const { return Meshes; }

	/** 网格包围盒中心（模型坐标，未缩放）。磁吸距离按它算，不按 Actor 原点：零件转一圈时原点会甩得很远。 */
	FVector GetLocalCenter() const { return LocalBounds.GetCenter(); }

	// IToyGrabbable
	virtual bool CanGrab() const override;
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override;
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual AToyHand* GetHolder() const override;
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override;
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 只用来检测手和鼠标射线的包围盒，不参与物理。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> GrabBox;

	/** 拿着时离原位多近（厘米）显示虚影，松手后快速吸回。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MagnetDistance = 5.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ReturnTime = 0.4f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SnapReturnTime = 0.12f;

	/** 检测盒最小半边长（世界厘米）：只防止 0.1cm 厚的垫圈盒子太薄，不再故意放大小件（判定已经改成指尖捏合点）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MinGrabHalfExtent = 0.3f;

private:
	FTransform FrameToHold(const FTransform& FrameWorld) const;
	FTransform CylinderFrame(const FToyGripContext& Grip) const;
	FTransform BoxFrame(const FToyGripContext& Grip) const;
	float DistanceToHome() const;
	void RefreshOverlay();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	TWeakObjectPtr<AToyExplodedModel> Model;
	TWeakObjectPtr<AToyHand> Holder;
	FString Id;
	FText Title;
	FText Body;
	EToyPartGrab Grab = EToyPartGrab::None;
	TArray<FToyCylinderSegment> Segments;
	FBox BoxShape = FBox(ForceInit);
	FBox LocalBounds = FBox(ForceInit);
	TArray<FToyExplodeStep> Steps;

	bool bGrabEnabled = false;
	int32 HoverCount = 0;
	TWeakObjectPtr<UMaterialInterface> ModelOverlay;

	bool bReturning = false;
	float ReturnAlpha = 0.f;
	float ReturnDuration = 0.4f;
	FTransform ReturnFrom;
	bool bGhostShown = false;
};
