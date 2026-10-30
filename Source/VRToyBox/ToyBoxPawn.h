#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "ToyHandCalib.h"
#include "ToyBoxPawn.generated.h"

class AToyFigurine;
class AToyGiantBody;
class AToyHand;
class AToyTouchScreen;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMotionControllerComponent;
class UProceduralMeshComponent;
struct FPredictProjectilePathPointData;

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

	/** toy.WalkTest：从工作室坐标 From 用摇杆走路的那套碰撞一步步走到 To，沿途每个挡路的东西、传送不了的地方都写进日志。 */
	void RunWalkTest(FVector2D From, FVector2D To);

	/** toy.ReachMap：在所有能传送的范围（外扩 1.5m）里按 Cell 厘米一格，看每一格是不是地板、能不能传送、
	 *  从开局站立点用摇杆走路（同一个胶囊体，相邻格子之间扫掠）走不走得到，挡路的是什么。写 Saved/ReachMap/<Name>.csv。 */
	void RunReachMap(float Cell, const FString& Name);

	/** toy.AimPreview：桌面模式下一直从右手朝 Angles（相对镜头）瞄准传送，截图用；关掉时收起抛物线。 */
	void SetAimPreview(bool bEnabled, FRotator Angles)
	{
		bAimPreview = bEnabled;
		AimPreviewAngles = Angles;
		if (!bEnabled)
		{
			bTeleportAiming = false;
			HideTeleportAim();
		}
	}

	/** 菜单的 CALIBRATE HANDS：进手部校准模式（和长按左摇杆一样）。不在 VR 里或已经在校准时返回 false。 */
	bool EnterHandCalibMode();

	/** 调试命令（toy.GrabAudit）接管右手时打开：桌面模式不再读鼠标、不再摆手。 */
	void SetExternalControl(bool bEnabled) { bExternalControl = bEnabled; }

	/** 绕头顶转身（VR 里摇杆转身用；toy.Look 在 VR 里也用它）。 */
	void SnapTurn(float Degrees);

	/** 小人国：在大身体和小人偶之间切换（黑屏淡出 → 换身体 → 淡入）。左手柄 Y / 桌面 G / toy.Shrink。 */
	void ToggleBody();
	bool IsTiny() const { return bTiny; }

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

	/** 传送抛物线：一根细管（芯）+ 外面一层很淡的粗管（光晕），挂在发出抛物线的那个组件（VR 里是右手）下面，跟着手柄的低延迟更新一起动。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UProceduralMeshComponent> TeleportArc;

	/** 落点圈：一圈两边虚的亮环 + 里面一层很淡的底，网格开局建一次。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UProceduralMeshComponent> TeleportRing;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyMode(bool bNewVR);
	void TickVR();
	void TickDesktop();

	/** 平滑移动：Input.X = 右，Input.Y = 前（相对头的水平朝向），带死区，用胶囊体扫掠挡墙和桌子。 */
	void SmoothMove(FVector2D Input, float DeltaSeconds);
	/** 胶囊体从脚下扫到 Move，碰到东西就停在碰撞点前再沿墙面滑一下；OutHit 不为空时写第一个挡路的东西。返回是否被挡。 */
	bool MoveWithCollision(FVector Move, FHitResult* OutHit = nullptr);
	/** 从 Start 沿 Direction 打抛物线，更新抛物线和落点圈的显示；返回落点是否能传送。 */
	bool UpdateTeleportAim(const FVector& Start, const FVector& Direction, USceneComponent* Source);
	void HideTeleportAim();
	/** 开局建好抛物线和落点圈的网格（三角形只建一次，之后每帧只改顶点位置和颜色）。 */
	void BuildTeleportMeshes();
	/** 把抛物线（世界坐标的路径点）重新采样成等距的一节节，更新管子的顶点；Space = 管子挂着的组件。 */
	void UpdateArcMesh(const TArray<FPredictProjectilePathPointData>& Points, const USceneComponent* Space);
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

	/** 小人国：开局在脚边放小人偶、准备好大人像。 */
	void SpawnLilliput();
	/** 换比例：WorldToMeters、近裁剪面、手、菜单、桌面模式的眼睛高度一起换。 */
	void ApplyPlayerScale(float Scale);
	/** 让脚落在 Target、身体朝 Rotation。HeadRatio = 新比例 / 旧比例：换比例那一帧头显的相对位置还是按旧比例算的。 */
	void PlaceFeetAt(const FVector& Target, const FRotator& Rotation, float HeadRatio);
	/** 小身体每帧贴地：脚下一个台阶（CapsuleLift × 比例）以内有东西就站上去，没有就按缩小后的重力往下掉。 */
	void TickTinyGround(float DeltaSeconds);
	/** 右手边信息牌显示几秒提示。 */
	void ShowToast(const FString& Title, const FString& Body, float Seconds = 4.f);

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
	TArray<FKey> SizeSwitchKeys;
	TArray<FKey> MenuKeys;
	bool bSizeSwitchWasDown = false;
	bool bMenuWasDown = false;

	// 小人国：两具身体
	bool bTiny = false;
	bool bSwitchingBody = false;
	/** 变小前大身体脚下的点、朝向、眼睛离脚多高（厘米）。 */
	FVector BigFeet = FVector::ZeroVector;
	FRotator BigRotation = FRotator::ZeroRotator;
	float BigEyeHeight = 165.f;
	/** 小身体往下掉的速度（厘米 / 秒，负数朝下）和上次站稳的点（掉出世界时回到这里）。 */
	float FallVelocity = 0.f;
	FVector LastGroundFeet = FVector::ZeroVector;
	float AppliedHandScale = 1.f;
	FTimerHandle ToastTimer;

	UPROPERTY(Transient)
	TObjectPtr<AToyFigurine> Figurine;

	UPROPERTY(Transient)
	TObjectPtr<AToyGiantBody> GiantBody;

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

	// 抛物线网格每帧复用的数组（开局按固定节数分配好，之后不再分配）
	TArray<FVector> ArcPath;
	TArray<float> ArcPathLength;
	TArray<FVector> ArcCoreVerts;
	TArray<FVector> ArcHaloVerts;
	TArray<FLinearColor> ArcCoreColors;
	TArray<FLinearColor> ArcHaloColors;
	int32 TeleportColorState = -1;
	bool bAimPreview = false;
	FRotator AimPreviewAngles = FRotator::ZeroRotator;

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
