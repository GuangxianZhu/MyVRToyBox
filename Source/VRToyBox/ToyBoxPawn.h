#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "ToyHandCalib.h"
#include "ToyBoxPawn.generated.h"

class AToyHand;
class AToyTouchScreen;
class UCameraComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMotionControllerComponent;
class UStaticMeshComponent;

/**
 * 震动混合器里的一层：等 Delay 秒后开始，Attack 秒升到 Amplitude，保持 Hold 秒，Decay 秒线性降到 0。
 * 每只手所有的层每帧加起来（再加上这一帧的持续驱动），截到 1，乘设置里的 HAPTICS 档位，只用振幅（Quest 上频率基本不起作用）。
 * 很短的层（比如 6ms 的"嗒"）比一帧还短：第一次轮到它时至少按满振幅输出一帧。
 */
struct FToyHapticLayer
{
	float Delay = 0.f;
	float Amplitude = 0.f;
	float Attack = 0.f;
	float Hold = 0.f;
	float Decay = 0.f;
	double Start = 0.0;
	bool bEmitted = false;
};

/**
 * 玩家：VROrigin（地面）+ 头显相机 + 两个动作控制器，控制器上各挂一只 AToyHand。
 *
 * 两种模式，每帧根据有没有头显自动切换：
 * - VR：扳机 = 拇指食指捏起 / 松开，握把 = 弯中指无名指小指（不参与抓），食指戳屏幕按钮；
 *   左摇杆 = 平滑移动（方向跟着头显朝向），右摇杆左右 = 45° 快速转身，右摇杆往前推 = 瞄准传送（抛物线 + 落点圈），松开传送。
 * - 桌面（没戴头显，方便在编辑器里快速看效果）：
 *   鼠标左键点按钮 / 按住左键捏起，松开放下；按住右键拖动转头；滚轮调远近；
 *   WASD = 平滑移动，按住 T = 从镜头沿鼠标方向瞄准传送，松开 T 传送。
 *
 * VR 手部校准（见 ToyHandCalib）：手 Actor 挂到握把姿态上时加一个校准变换；长按左摇杆 1 秒进 / 出校准模式，
 * 模式里摇杆调角度 / 位置，A 切换旋转页 / 位置页，B 恢复默认，退出时存盘。
 */
UCLASS()
class VRTOYBOX_API AToyBoxPawn : public APawn
{
	GENERATED_BODY()

public:
	AToyBoxPawn();

	virtual void Tick(float DeltaSeconds) override;

	/** 一下固定强度的震动（加一层没有渐入渐出的震动层）。 */
	void PlayHaptic(EControllerHand Hand, float Amplitude, float Duration);

	/** 给某只手的震动混合器加一层（UToyFeedback 用）。 */
	void AddHaptic(EControllerHand Hand, const FToyHapticLayer& Layer);

	/** 这一帧的持续震动（比如手指在动），每帧要重新设，不设就是 0。同一帧设多次取最大。 */
	void SetHapticDrive(EControllerHand Hand, float Amplitude);

	/** toy.HapticTest：在右手上放一串测试震动，右手边的信息牌显示正在放哪一步。 */
	void StartHapticTest(int32 Mode);

	AToyHand* GetRightHand() const { return RightHand; }

	/** 菜单的 CALIBRATE HANDS：进手部校准模式（和长按左摇杆一样）。不在 VR 里或已经在校准时返回 false。 */
	bool EnterHandCalibMode();

	/** 调试命令（toy.GrabAudit）接管右手时打开：桌面模式不再读鼠标、不再摆手。 */
	void SetExternalControl(bool bEnabled) { bExternalControl = bEnabled; }

	/** 绕头顶转身（VR 里摇杆转身用；toy.Look 在 VR 里也用它）。 */
	void SnapTurn(float Degrees);

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> RightController;

	/** 瞄准姿态（传送抛物线从右手的这个姿态发出）和掌心姿态（只做诊断：进 VR 时打一次相对握把的变换）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> LeftAim;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> RightAim;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> LeftPalm;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> RightPalm;

	/** 平滑转身的速度（度 / 秒，设置里选了 SMOOTH 时用）。分段转身的角度、移动速度见 ToySettings。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SmoothTurnSpeed = 90.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float DesktopEyeHeight = 165.f;

	/** 左摇杆平滑移动的死区（速度在设置里选）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MoveDeadZone = 0.2f;

	/** 挡墙和桌子用的胶囊体（半径、半高，厘米）。胶囊体底部离地 CapsuleLift，跨得过地毯。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float CapsuleRadius = 20.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float CapsuleHalfHeight = 70.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float CapsuleLift = 15.f;

	/** 传送抛物线的出射速度（厘米 / 秒），越大越远。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float TeleportLaunchSpeed = 650.f;

	/** 传送时黑屏淡出 + 淡入各用多久（秒）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float TeleportFadeTime = 0.15f;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UInstancedStaticMeshComponent> TeleportArc;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> TeleportRing;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyMode(bool bNewVR);
	void TickVR();
	void TickDesktop();

	/** 平滑移动：Input.X = 右，Input.Y = 前（相对头的水平朝向），带死区，用胶囊体扫掠挡墙和桌子。 */
	void SmoothMove(FVector2D Input, float DeltaSeconds);
	/** 从 Start 沿 Direction 打抛物线，更新抛物线和落点圈的显示；返回落点是否能传送。 */
	bool UpdateTeleportAim(const FVector& Start, const FVector& Direction);
	void HideTeleportAim();
	/** 黑屏淡出 → 把头挪到 Target 正上方（脚在 Target 的高度）→ 淡入。 */
	void Teleport(const FVector& Target);
	bool IsTeleportTargetValid(const FHitResult& Hit) const;
	FVector GetFeetLocation() const;

	/** 把现在的手部校准套到两只手（VR 下挂在握把上的相对变换）。 */
	void ApplyHandCalib();
	/** 桌面模式开局：打印手指方向在握把系里是什么样（校准后应该约 (0.44, 0, -0.90)）。 */
	void LogHandSelfCheck() const;
	/** VR 追踪生效后打印一次瞄准 / 掌心姿态相对握把的变换。 */
	void LogPoseDiagnostics();
	void ToggleHandCalibMode();
	void TickHandCalibMode(float DeltaSeconds);
	void AddIgnoredForMove(FCollisionQueryParams& Params) const;
	float ReadAxis(const TArray<FKey>& Keys) const;
	AToyHand* SpawnHand(EControllerHand HandSide, UMotionControllerComponent* MotionController);
	void TickHaptics();

	UPROPERTY(Transient)
	TObjectPtr<AToyHand> LeftHand;

	UPROPERTY(Transient)
	TObjectPtr<AToyHand> RightHand;

	// 同一个动作在不同头显上是不同的键，全读一遍取最大值
	TArray<FKey> GripLeftKeys;
	TArray<FKey> GripRightKeys;
	TArray<FKey> TriggerLeftKeys;
	TArray<FKey> TriggerRightKeys;
	TArray<FKey> TurnKeys;
	TArray<FKey> MoveXKeys;
	TArray<FKey> MoveYKeys;
	TArray<FKey> TeleportKeys;
	TArray<FKey> CalibToggleKeys;
	TArray<FKey> CalibPageKeys;
	TArray<FKey> CalibResetKeys;

	ToyHandCalib::FParams AppliedCalib;
	bool bCalibApplied = false;
	bool bPoseDiagnosticsLogged = false;
	bool bCalibMode = false;
	bool bCalibPositionPage = false;
	bool bCalibToggleArmed = true;
	bool bCalibPageWasDown = false;
	bool bCalibResetWasDown = false;
	float CalibHoldTime = 0.f;
	/** 上次震动时各参数所在的"格子"（1° / 0.5cm 一格），换格就震一下。 */
	FIntVector CalibRotStep = FIntVector::ZeroValue;
	FIntVector CalibPosStep = FIntVector::ZeroValue;

	bool bTeleportAiming = false;
	bool bTeleportValid = false;
	bool bTeleporting = false;
	FVector TeleportTarget = FVector::ZeroVector;
	FTimerHandle TeleportTimer;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TeleportMaterial;

	bool bVR = false;
	bool bExternalControl = false;
	bool bModeApplied = false;
	bool bInputModeApplied = false;
	bool bSnapTurnArmed = true;

	bool bDesktopClickIsPoke = false;

	/** 桌面模式下鼠标正指着的触屏（移开时告诉它，按钮不再悬停高亮）。 */
	TWeakObjectPtr<AToyTouchScreen> HoverScreen;
	bool bHadMousePosition = false;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	float DesktopHoldDistance = 60.f;

	/** 震动混合器：[0] = 左手，[1] = 右手。 */
	TArray<FToyHapticLayer> HapticLayers[2];
	float HapticDrive[2] = { 0.f, 0.f };
	float HapticOutput[2] = { 0.f, 0.f };

	/** toy.HapticTest 每一步的开始时间（相对 HapticTestStart）和信息牌上显示的字。 */
	TArray<TPair<float, FString>> HapticTestSteps;
	double HapticTestStart = 0.0;
	int32 HapticTestShown = INDEX_NONE;
};
