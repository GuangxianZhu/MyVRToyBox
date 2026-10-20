#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "ToyHand.generated.h"

class AToyBoxPawn;
class IToyGrabbable;
class STextBlock;
class UArrowComponent;
class UPrimitiveComponent;
class UWidgetComponent;

/**
 * 用基础形状拼出来的一只手，挂在动作控制器（握把姿态）上。
 *
 * 坐标约定（右手，左手由 VisualRoot 的 Y 轴镜像得到）：
 *   手指朝 +X，掌心朝 -Y，拇指在 +Z 一侧，原点 = 控制器握把姿态。
 *
 * - GripPoint：手掌表面的抓点。X 轴从掌心指向手背，Z 轴指向手指。
 *   方块的抓点用同样约定（X = 面的外法线），两者重合 = 手掌贴住方块的一个面。
 * - PokeTip：食指指尖，用来戳触屏。
 * - PinchPoint：拇指和食指指尖之间，小零件"捏"在这里。
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

	/** 桌面调试模式：把手摆到让 GripPoint 落在 GripWorldLocation 上。 */
	void SetDesktopPose(const FVector& GripWorldLocation, const FRotator& Facing);

	void SetPokeEnabled(bool bEnabled);
	void ReleaseHeld(bool bThrow);
	bool IsHolding() const;
	AActor* GetHeldActor() const;

	/** 桌面模式：鼠标射线点中的 Actor。它在手的抓取范围里时优先抓它（零件挨得近时，以鼠标点中的为准）。 */
	void SetPreferredTarget(AActor* Target) { PreferredTarget = Target; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 所有可见的手部形状都挂在这里；左手把它的 Y 缩放设成 -1 来镜像。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UArrowComponent> GripPoint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> PokeTip;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> PinchPoint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UWidgetComponent> InfoLabel;

	/** 抓取检测半径（从 GripPoint 算起）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float GrabRadius = 9.f;

	/** 抓起时方块从原位置滑到手里的时间。0 = 瞬间吸附。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SnapTime = 0.08f;

	/** 全手握着东西时手指至少弯这么多度，看起来像握住了 10cm 的方块。捏和托用更小的角度（见 TickCurls）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float HoldCurl = 55.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MaxCurl = 85.f;

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
	void TickPoke();
	void RecordMotion();
	FVector GetThrowVelocity() const;
	void Pulse(float Amplitude, float Duration) const;

	/** 每根手指两个关节：[手指序号 * 2] = 指根，[手指序号 * 2 + 1] = 中节。0 号是食指。 */
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> FingerJoints;

	UPROPERTY()
	TObjectPtr<USceneComponent> ThumbBase;

	UPROPERTY()
	TObjectPtr<USceneComponent> ThumbMid;

	TWeakObjectPtr<AToyBoxPawn> OwnerPawn;
	EControllerHand Side = EControllerHand::Right;

	float Grip = 0.f;
	float Trigger = 0.f;
	float FingerCurl = 0.f;
	float IndexCurl = 0.f;
	bool bGripDown = false;
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

	struct FMotionSample
	{
		FVector Location;
		double Time;
	};
	TArray<FMotionSample> MotionSamples;
};
