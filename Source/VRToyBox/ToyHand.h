#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "ToyHand.generated.h"

class AToyBoxPawn;
class IToyGrabbable;
class STextBlock;
class UAnimSequence;
class UArrowComponent;
class UMaterialInstanceDynamic;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class USkeletalMesh;
class UWidgetComponent;

/**
 * 一只手，挂在动作控制器（握把姿态）上。模型用 UE VR 模板自带的 MannyXR 手（Content/XRMannequins），
 * 左右手各用自己的网格；手指不用动画蓝图，而是每帧在"张开"（Idle）和"握拳"（Grasp）两个姿势之间按手指分组插值：
 * 扳机 → 拇指和食指合拢成"捏"；握把 → 中指、无名指、小指（见 TickCurls / ApplyFingerPose）。
 *
 * 抓取只看扳机（像拇指食指捏东西）：扳机过 0.6 捏起、低于 0.4 松开；握把完全不参与抓取。
 * 捏合点处画一个半透明小球（RangeSphere，半径 = GrabRadius）：平时淡白，有东西进范围变绿，捏住后缩小变淡。
 *
 * 坐标约定（右手；左手是镜像）：
 *   手指朝 +X，掌心朝 -Y（左手 +Y），原点 = 控制器握把姿态。网格怎么摆由 InitHand 从骨骼自动算：
 *   手腕→中指根 = +X，手指握拳时指尖移动的方向 = 掌心方向。
 *
 * - GripPoint：手掌表面的抓点。X 轴从掌心指向手背，Z 轴指向手指。
 *   方块的抓点用同样约定（X = 面的外法线），两者重合 = 手掌贴住方块的一个面。
 * - PokeTip：食指指尖（每帧对到食指末节骨骼的指尖），用来戳触屏。
 * - 捏合点：拇指指尖和食指指尖的中点，跟着手指弯曲实时变化。抓取判定只在这里，小零件也"捏"在这里。
 * - InfoLabel：拿着零件时浮在零件上方、始终面向玩家的信息牌（Slate 文字，能显示中文）。
 *
 * 能抓的东西都实现 IToyGrabbable（方块、电机零件、整台电机），手只认这个接口。
 */
UCLASS()
class VRTOYBOX_API AToyHand : public AActor
{
	GENERATED_BODY()

public:
	AToyHand();

	virtual void Tick(float DeltaSeconds) override;

	/** 生成后调用一次：决定左右手（镜像）和抓点位置。 */
	void InitHand(AToyBoxPawn* InPawn, EControllerHand InSide);

	/** 每帧由 Pawn 喂进来，0~1。握把决定抓/放和三根手指的弯曲，扳机决定食指弯曲。 */
	void SetInputs(float InGrip, float InTrigger);

	/** 桌面调试模式：把手摆到让捏合点落在 PinchWorldLocation 上（鼠标点中的位置）。 */
	void SetDesktopPose(const FVector& PinchWorldLocation, const FRotator& Facing);

	/** 捏合点：拇指指尖和食指指尖的中点（世界坐标）。抓取和悬停高亮都只在这个点附近判定。 */
	FVector GetPinchLocation() const;

	void SetPokeEnabled(bool bEnabled);
	void ReleaseHeld(bool bThrow);
	bool IsHolding() const;
	AActor* GetHeldActor() const;

	/** 桌面模式：鼠标射线点中的 Actor。它在手的抓取范围里时优先抓它（零件挨得近时，以鼠标点中的为准）。 */
	void SetPreferredTarget(AActor* Target) { PreferredTarget = Target; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** MannyXR 手模型。骨骼局部变换由 ApplyFingerPose 每帧直接写。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UPoseableMeshComponent> HandMesh;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UArrowComponent> GripPoint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> PokeTip;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UWidgetComponent> InfoLabel;

	/** 捏合点的范围小球：只显示，不碰撞、不投影。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> RangeSphere;

	/** 扳机按过这个值捏起，松到 ReleaseThreshold 以下放开（迟滞，防止扳机在中间值抖动时反复抓放）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PinchThreshold = 0.6f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ReleaseThreshold = 0.4f;

	/** 扳机按满时食指和拇指的弯曲程度（0 = 张开，1 = 握拳），这个组合下两个指尖刚好碰在一起。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PinchIndexCurl = 0.45f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PinchThumbCurl = 0.55f;

	/** 抓取判定半径：捏合点到物体碰撞表面的距离不超过这么多（厘米）才算碰到。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float GrabRadius = 2.f;

	/** 抓起时方块从原位置滑到手里的时间。0 = 瞬间吸附。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SnapTime = 0.08f;

	/** 全手握着东西时手指至少弯到"握拳"的这么多（0~1），看起来像握住了 10cm 的方块。捏和托见 TickCurls。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float HoldCurl = 0.6f;

	/** 现在有没有捏住东西 / 有没有东西在捏合范围里（范围小球的颜色用，调试也用）。 */
	bool IsPinchHovering() const { return HoveredActor.IsValid(); }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* FindGrabCandidate() const;
	void Grab(AActor* Target);
	void UpdateHover();
	void ShowInfo(AActor* Target);
	void TickInfo();
	void TickHeld(float DeltaSeconds);
	void TickCurls(float DeltaSeconds);
	void SetupHandMesh();
	void ApplyFingerPose();
	FVector GetFingerTip(int32 TipBone, int32 PrevBone) const;
	void TickPoke();
	void RecordMotion();
	FVector GetThrowVelocity() const;
	void Pulse(float Amplitude, float Duration) const;
	void TickRangeSphere(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<USkeletalMesh> RightMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> LeftMesh;

	UPROPERTY()
	TObjectPtr<UAnimSequence> OpenAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> FistAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> IndexCurlAnim;

	UPROPERTY()
	TObjectPtr<UAnimSequence> ThumbUpAnim;

	/** 每根骨骼（网格骨骼序号）的张开 / 握拳局部变换和所属手指组：0 = 不动，1 = 食指，2 = 中指无名指小指，3 = 拇指。 */
	TArray<FTransform> OpenPose;
	TArray<FTransform> FistPose;
	TArray<uint8> BoneGroup;
	int32 IndexTipBone = INDEX_NONE;
	int32 IndexMidBone = INDEX_NONE;
	int32 ThumbTipBone = INDEX_NONE;
	int32 ThumbMidBone = INDEX_NONE;
	float ThumbCurl = 0.f;

	TWeakObjectPtr<AToyBoxPawn> OwnerPawn;
	EControllerHand Side = EControllerHand::Right;

	float Grip = 0.f;
	float Trigger = 0.f;
	float FingerCurl = 0.f;
	float IndexCurl = 0.f;
	bool bTriggerDown = false;
	bool bPokeEnabled = true;

	TWeakObjectPtr<AActor> HeldActor;
	TWeakObjectPtr<AActor> HoveredActor;
	TWeakObjectPtr<AActor> PreferredTarget;
	TSharedPtr<STextBlock> InfoTitle;
	TSharedPtr<STextBlock> InfoBody;
	FTransform SnapFrom;
	FTransform SnapTo;
	float SnapAlpha = 1.f;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Touching;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RangeMaterial;
	float RangeScale = 1.f;

	struct FMotionSample
	{
		FVector Location;
		double Time;
	};
	TArray<FMotionSample> MotionSamples;
};
