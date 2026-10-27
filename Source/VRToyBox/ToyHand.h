#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "ToyHand.generated.h"

class AToyBoxPawn;
class AToyTouchScreen;
class IToyGrabbable;
class STextBlock;
class UAnimSequence;
class UArrowComponent;
class UMaterialInstanceDynamic;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UToyClawToolRig;
class UToyDexHandRig;
class UMaterialInterface;
class UPrimitiveComponent;
class USkeletalMesh;
class UWidgetComponent;
class UAudioComponent;
enum class EToyFeedback : uint8;

/**
 * 一只手，挂在动作控制器（握把姿态）上。默认是三指抓取器（参考 Red Matter 2，见 UToyClawToolRig）：
 * 戴手套的手（MannyXR 手固定握拳、换成手套色）握着握柄，扳机按多少三根手指就合多少，
 * 捏住零件后每根手指各自停在碰到零件表面的地方（见 SetupClaw / FindClawContact）。
 * 控制台 toy.HandModel 0 / 1 还能临时换回 UE VR 模板的 MannyXR 手（Content/XRMannequins）或五指灵巧手（Content/Models/DexHand，
 * 见 UToyDexHandRig），只用来对比，设置页里已经没有这个选项。
 * 下面关于手指的说明是 MannyXR / 灵巧手的；抓取器用 ClawClose（每根手指 0 = 张开，1 = 合拢）。
 * 两种手的手指都按手指分组的弯曲程度摆：扳机 → 拇指和食指合拢成"捏"；握把 → 中指、无名指、小指（见 TickCurls / ApplyFingerPose）。
 * MannyXR 不用动画蓝图，而是每帧在"张开"（Idle）和"握拳"（Grasp）两个姿势之间插值；灵巧手按关节表插值关节角。
 * 捏住东西后，拇指和食指各自只弯到指尖碰到物体表面为止（见 ComputeContactCurls，控制台 toy.FingerContact 0 关掉）。
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

	/** 捏合点（世界坐标）：扳机按满时拇指指尖和食指指尖的中点，锚定在手上的一个固定点，
	 *  不随手指弯曲移动（扣扳机的过程中判定点和范围小球不会跟着手指跑）。抓取和悬停高亮都只在这个点附近判定。 */
	FVector GetPinchLocation() const;

	/** 手腕 → 中指根的方向（手 Actor 坐标系，单位向量）。桌面自检用：VR 校准后这个方向在握把系里应该约 (0.44, 0, -0.90)。 */
	FVector GetFingerDirectionLocal() const;

	/** 在手上方显示 / 收起一块自定义信息牌（校准模式用），拿着东西时的零件信息牌优先级更低。 */
	void ShowCustomInfo(const FText& Title, const FText& Body);
	void HideCustomInfo();

	void SetPokeEnabled(bool bEnabled);
	void ReleaseHeld(bool bThrow);
	bool IsHolding() const;
	EControllerHand GetSide() const { return Side; }
	AActor* GetHeldActor() const;

	/** 桌面模式：鼠标射线点中的 Actor。它在手的抓取范围里时优先抓它（零件挨得近时，以鼠标点中的为准）。 */
	void SetPreferredTarget(AActor* Target) { PreferredTarget = Target; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** MannyXR 手模型。骨骼局部变换由 ApplyFingerPose 每帧直接写。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UPoseableMeshComponent> HandMesh;

	/** 五指灵巧手（设置页 HAND MODEL = ROBOT）。弯曲程度由 ApplyFingerPose 每帧喂进去。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyDexHandRig> DexHand;

	/** 三指抓取器（默认的手）：网格和手指运动见 UToyClawToolRig。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyClawToolRig> ClawTool;

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

	/** 抓取判定半径：捏合点到物体碰撞表面的距离不超过这么多（厘米）才算碰到。每帧按设置（ToySettings::GrabRadiusCm）覆盖。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float GrabRadius = 2.f;

	/** 抓起时方块从原位置滑到手里的时间。0 = 瞬间吸附。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SnapTime = 0.08f;

	/** 全手握着东西时手指至少弯到"握拳"的这么多（0~1），看起来像握住了 10cm 的方块。捏和托见 TickCurls。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float HoldCurl = 0.6f;

	/** 指尖贴表面时把指尖当成这么粗的球（半径，厘米），沿弯曲路径扫过去碰到物体就停，免得指头陷进表面。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float FingerTipRadius = 0.6f;

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
	bool SetupDexHand();
	void SetupMannyHand();
	void SetupClaw();
	/** 抓取器模式：拇指 IK 压到面板上（结果存在 ThumbOverride，PoseManny 每次摆手时套上）。 */
	void SolveGloveThumb();
	void SetClawVisible(bool bVisible);
	/** 抓取器第 Finger 根手指从张开合到底的路上第一次碰到拿着的东西时的合拢程度；一路没碰到 = 1。 */
	float FindClawContact(int32 Finger) const;
	/** MannyXR 手按三组弯曲程度摆骨骼；没有骨骼资产返回 false。 */
	bool PoseManny(float InIndexCurl, float InFingerCurl, float InThumbCurl);
	void ApplyFingerPose();
	FVector GetFingerTip(int32 TipBone, int32 PrevBone) const;
	FVector FingerTipAtCurl(uint8 Group, float Curl, int32 TipBone, int32 PrevBone) const;
	float FindContactCurl(uint8 Group, float MaxCurl, int32 TipBone, int32 PrevBone) const;
	void ComputeContactCurls();
	void TickPoke();
	void RecordMotion();
	FVector GetThrowVelocity() const;
	void Pulse(float Amplitude, float Duration) const;
	/** 声音 + 震动事件（UToyFeedback），震这只手；给了 Target 就按它的材料和大小。 */
	void Feedback(EToyFeedback Event, const FVector& Location, const AActor* Target, float Speed = 0.f) const;
	/** 伺服电机声 + 齿轮感震动，跟着三根手指的合拢速度（只有抓取器有）。 */
	void TickServo(const float* PreviousClose, float DeltaSeconds);
	/** 拿着东西时：每根手指碰到零件表面响一声，三根都到位再来一下锁定。 */
	void TickContactFeedback();
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
	/** 抓取器模式下拇指三节的骨骼序号和解好的局部变换（空 = 不覆盖）。 */
	TArray<int32> ThumbOverrideBones;
	TArray<FTransform> ThumbOverride;
	/** 抓取器模式下中指 / 无名指 / 小指各关节自己的弯曲程度（按骨骼序号，-1 = 用所属手指组的弯曲程度）。 */
	TArray<float> BoneWeightOverride;
	TArray<uint8> BoneGroup;
	int32 IndexTipBone = INDEX_NONE;
	int32 IndexMidBone = INDEX_NONE;
	int32 ThumbTipBone = INDEX_NONE;
	int32 ThumbMidBone = INDEX_NONE;
	int32 WristBoneIndex = INDEX_NONE;
	int32 MiddleBoneIndex = INDEX_NONE;
	/** 捏合点在手 Actor 坐标系里的固定位置（SetupHandMesh 里按"扳机按满"的手势算一次）。 */
	FVector PinchAnchor = FVector::ZeroVector;
	bool bHasPinchAnchor = false;
	/** 现在用的是灵巧手（否则 MannyXR），以及摆手时的 toy.HandModel / toy.DexHandScale（变了就重摆）。 */
	bool bUseDex = false;
	bool bUseClaw = false;
	/** 抓取器三根手指现在的合拢程度，以及拿着东西时各自停在哪里。 */
	float ClawClose[3] = {};
	float ClawContact[3] = { 1.f, 1.f, 1.f };
	int32 AppliedHandModel = -1;
	float AppliedDexScale = 1.f;
	bool bCustomInfo = false;
	float ThumbCurl = 0.f;
	/** 捏住东西时拇指 / 食指弯到哪里停（指尖碰到物体表面）。抓起后等吸附滑完算一次，零件挂在手上不动，之后不用再算。 */
	float ContactIndexCurl = 0.f;
	float ContactThumbCurl = 0.f;
	bool bContactPending = false;
	/** 抓取器：每根手指碰到零件的声音已经响过；三根都到位后的锁定声还没响。 */
	bool bFingerTouched[3] = { false, false, false };
	bool bSecurePending = false;
	/** 伺服齿轮感：手指合拢量累计到 0.08 震一下。 */
	float ServoAccum = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ServoLoop;

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

	/** 指尖现在在哪块触屏的感应区里（离开时要告诉它松开）。 */
	TWeakObjectPtr<AToyTouchScreen> TouchingScreen;

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
