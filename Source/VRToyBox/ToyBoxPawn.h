#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "ToyBoxPawn.generated.h"

class AToyHand;
class UCameraComponent;
class UMotionControllerComponent;

/**
 * 玩家：VROrigin（地面）+ 头显相机 + 两个动作控制器，控制器上各挂一只 AToyHand。
 *
 * 两种模式，每帧根据有没有头显自动切换：
 * - VR：握把 = 抓/放，扳机 = 弯食指，右摇杆左右 = 45° 快速转身，食指戳屏幕按钮。
 * - 桌面（没戴头显，方便在编辑器里快速看效果）：
 *   鼠标左键点按钮 / 按住左键抓方块，松开扔出；按住右键拖动转头；滚轮调远近。
 */
UCLASS()
class VRTOYBOX_API AToyBoxPawn : public APawn
{
	GENERATED_BODY()

public:
	AToyBoxPawn();

	virtual void Tick(float DeltaSeconds) override;

	void PlayHaptic(EControllerHand Hand, float Amplitude, float Duration);

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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyMode(bool bNewVR);
	void TickVR();
	void TickDesktop();
	void SnapTurn(float Degrees);
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

	bool bVR = false;
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
