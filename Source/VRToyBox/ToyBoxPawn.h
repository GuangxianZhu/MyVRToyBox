#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "ToyBoxPawn.generated.h"

class AToyHand;
class UCameraComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMotionControllerComponent;
class UStaticMeshComponent;

/**
 * 玩家：VROrigin（地面）+ 头显相机 + 两个动作控制器，控制器上各挂一只 AToyHand。
 *
 * 两种模式，每帧根据有没有头显自动切换：
 * - VR：扳机 = 拇指食指捏起 / 松开，握把 = 弯中指无名指小指（不参与抓），食指戳屏幕按钮；
 *   左摇杆 = 平滑移动（方向跟着头显朝向），右摇杆左右 = 45° 快速转身，右摇杆往前推 = 瞄准传送（抛物线 + 落点圈），松开传送。
 * - 桌面（没戴头显，方便在编辑器里快速看效果）：
 *   鼠标左键点按钮 / 按住左键捏起，松开放下；按住右键拖动转头；滚轮调远近；
 *   WASD = 平滑移动，按住 T = 从镜头沿鼠标方向瞄准传送，松开 T 传送。
 */
UCLASS()
class VRTOYBOX_API AToyBoxPawn : public APawn
{
	GENERATED_BODY()

public:
	AToyBoxPawn();

	virtual void Tick(float DeltaSeconds) override;

	void PlayHaptic(EControllerHand Hand, float Amplitude, float Duration);

	AToyHand* GetRightHand() const { return RightHand; }

	/** 调试命令（toy.GrabAudit）接管右手时打开：桌面模式不再读鼠标、不再摆手。 */
	void SetExternalControl(bool bEnabled) { bExternalControl = bEnabled; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UMotionControllerComponent> RightController;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float SnapTurnDegrees = 45.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float DesktopEyeHeight = 165.f;

	/** 左摇杆平滑移动的速度（厘米 / 秒）和死区。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MoveSpeed = 150.f;

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
	void SnapTurn(float Degrees);

	/** 平滑移动：Input.X = 右，Input.Y = 前（相对头的水平朝向），带死区，用胶囊体扫掠挡墙和桌子。 */
	void SmoothMove(FVector2D Input, float DeltaSeconds);
	/** 从 Start 沿 Direction 打抛物线，更新抛物线和落点圈的显示；返回落点是否能传送。 */
	bool UpdateTeleportAim(const FVector& Start, const FVector& Direction);
	void HideTeleportAim();
	/** 黑屏淡出 → 把头挪到 Target 正上方（脚在 Target 的高度）→ 淡入。 */
	void Teleport(const FVector& Target);
	bool IsTeleportTargetValid(const FHitResult& Hit) const;
	FVector GetFeetLocation() const;
	float ReadAxis(const TArray<FKey>& Keys) const;
	AToyHand* SpawnHand(EControllerHand HandSide, UMotionControllerComponent* MotionController);

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
	bool bHadMousePosition = false;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	float DesktopHoldDistance = 60.f;

	FTimerHandle LeftHapticTimer;
	FTimerHandle RightHapticTimer;
};
