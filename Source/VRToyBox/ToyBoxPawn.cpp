#include "ToyBoxPawn.h"

#include "ToyCube.h"
#include "ToyHand.h"
#include "ToyTouchScreen.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MotionControllerComponent.h"
#include "TimerManager.h"

AToyBoxPawn::AToyBoxPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// 转身由我们自己改 Actor 旋转，不让 PlayerController 的旋转覆盖它
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true;

	// "LeftGrip"/"RightGrip" = 握把姿态：原点大约在手心握住手柄的位置
	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(FName(TEXT("LeftGrip")));

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(FName(TEXT("RightGrip")));

	// 这些键也写在 Config/DefaultInput.ini 里，OpenXR 才会去绑定它们
	GripLeftKeys = { FKey(TEXT("OculusTouch_Left_Grip_Axis")), FKey(TEXT("ValveIndex_Left_Grip_Axis")),
		FKey(TEXT("MixedReality_Left_Grip_Click")), FKey(TEXT("Vive_Left_Grip_Click")) };
	GripRightKeys = { FKey(TEXT("OculusTouch_Right_Grip_Axis")), FKey(TEXT("ValveIndex_Right_Grip_Axis")),
		FKey(TEXT("MixedReality_Right_Grip_Click")), FKey(TEXT("Vive_Right_Grip_Click")) };
	TriggerLeftKeys = { FKey(TEXT("OculusTouch_Left_Trigger_Axis")), FKey(TEXT("ValveIndex_Left_Trigger_Axis")),
		FKey(TEXT("MixedReality_Left_Trigger_Axis")), FKey(TEXT("Vive_Left_Trigger_Axis")) };
	TriggerRightKeys = { FKey(TEXT("OculusTouch_Right_Trigger_Axis")), FKey(TEXT("ValveIndex_Right_Trigger_Axis")),
		FKey(TEXT("MixedReality_Right_Trigger_Axis")), FKey(TEXT("Vive_Right_Trigger_Axis")) };
	TurnKeys = { FKey(TEXT("OculusTouch_Right_Thumbstick_X")), FKey(TEXT("ValveIndex_Right_Thumbstick_X")),
		FKey(TEXT("MixedReality_Right_Thumbstick_X")), FKey(TEXT("Vive_Right_Trackpad_X")) };
}

void AToyBoxPawn::BeginPlay()
{
	Super::BeginPlay();
	LeftHand = SpawnHand(EControllerHand::Left, LeftController);
	RightHand = SpawnHand(EControllerHand::Right, RightController);
}

AToyHand* AToyBoxPawn::SpawnHand(EControllerHand HandSide, UMotionControllerComponent* MotionController)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToyHand* Hand = GetWorld()->SpawnActor<AToyHand>(AToyHand::StaticClass(), GetActorTransform(), Params);
	if (Hand)
	{
		Hand->InitHand(this, HandSide);
		// 先让 Pawn 读完输入、控制器更新完姿态，手再 Tick
		Hand->AddTickPrerequisiteActor(this);
		Hand->AddTickPrerequisiteComponent(MotionController);
	}
	return Hand;
}

void AToyBoxPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LeftHand)
	{
		LeftHand->Destroy();
	}
	if (RightHand)
	{
		RightHand->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AToyBoxPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!LeftHand || !RightHand)
	{
		return;
	}

	// VR Preview 和 -vr 启动时，立体渲染可能比 BeginPlay 晚开启，所以每帧检查
	const bool bHmd = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
	if (!bModeApplied || bHmd != bVR)
	{
		ApplyMode(bHmd);
	}

	if (!bInputModeApplied)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->SetShowMouseCursor(!bVR);
			if (bVR)
			{
				PC->SetInputMode(FInputModeGameOnly());
			}
			else
			{
				FInputModeGameAndUI Mode;
				Mode.SetHideCursorDuringCapture(false);
				PC->SetInputMode(Mode);
			}
			bInputModeApplied = true;
		}
	}

	if (bVR)
	{
		TickVR();
	}
	else
	{
		TickDesktop();
	}
}

void AToyBoxPawn::ApplyMode(bool bNewVR)
{
	bVR = bNewVR;
	bModeApplied = true;
	bInputModeApplied = false;

	if (bVR)
	{
		// 追踪原点设在地面，这样 VROrigin 放在地板上、相机高度就是你的真实身高。
		// 不同引擎版本里这个枚举叫 Floor 或 LocalFloor，但数值都是 1，用 decltype 取类型避免名字差异
		using FTrackingOrigin = decltype(UHeadMountedDisplayFunctionLibrary::GetTrackingOrigin());
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(static_cast<FTrackingOrigin>(1));

		Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		LeftHand->AttachToComponent(LeftController, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		RightHand->AttachToComponent(RightController, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		LeftHand->SetActorHiddenInGame(false);
		LeftHand->SetActorTickEnabled(true);
		LeftHand->SetPokeEnabled(true);
		RightHand->SetPokeEnabled(true);
	}
	else
	{
		Camera->SetRelativeLocationAndRotation(FVector(0.f, 0.f, DesktopEyeHeight), FRotator(-40.f, 0.f, 0.f));
		LeftHand->ReleaseHeld(false);
		LeftHand->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		LeftHand->SetActorHiddenInGame(true);
		LeftHand->SetActorTickEnabled(false);
		RightHand->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		// 桌面模式下手跟着鼠标走，靠鼠标点击来按按钮，避免手划过屏幕时误触
		RightHand->SetPokeEnabled(false);
	}
}

float AToyBoxPawn::ReadAxis(const TArray<FKey>& Keys) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return 0.f;
	}
	float Best = 0.f;
	for (const FKey& Key : Keys)
	{
		float Value = PC->GetInputAnalogKeyState(Key);
		// 纯按键（如 Vive 的握把）没有模拟量，按下就当 1
		if (FMath::IsNearlyZero(Value) && PC->IsInputKeyDown(Key))
		{
			Value = 1.f;
		}
		if (FMath::Abs(Value) > FMath::Abs(Best))
		{
			Best = Value;
		}
	}
	return Best;
}

void AToyBoxPawn::TickVR()
{
	LeftHand->SetInputs(ReadAxis(GripLeftKeys), ReadAxis(TriggerLeftKeys));
	RightHand->SetInputs(ReadAxis(GripRightKeys), ReadAxis(TriggerRightKeys));

	// 快速转身：摇杆推过 0.7 转一次，回到 0.3 以内才能再转
	const float Turn = ReadAxis(TurnKeys);
	if (bSnapTurnArmed && FMath::Abs(Turn) > 0.7f)
	{
		SnapTurn(Turn > 0.f ? SnapTurnDegrees : -SnapTurnDegrees);
		bSnapTurnArmed = false;
	}
	else if (FMath::Abs(Turn) < 0.3f)
	{
		bSnapTurnArmed = true;
	}
}

void AToyBoxPawn::SnapTurn(float Degrees)
{
	// 绕着头的位置转，而不是绕 VROrigin 转：否则人在房间里走开后再转身，世界会绕着脚下的原点甩一大圈
	const FVector Pivot = Camera->GetComponentLocation();
	const FRotator Delta(0.f, Degrees, 0.f);
	const FVector NewLocation = Pivot + Delta.RotateVector(GetActorLocation() - Pivot);
	SetActorLocationAndRotation(NewLocation, GetActorRotation() + Delta);
}

void AToyBoxPawn::TickDesktop()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// 按住右键拖动 = 转头（用光标位置差，光标显示时也能用）
	float MouseX = 0.f;
	float MouseY = 0.f;
	const bool bHaveMouse = PC->GetMousePosition(MouseX, MouseY);
	if (bHaveMouse && bHadMousePosition && PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		FRotator Look = Camera->GetRelativeRotation();
		Look.Yaw += (MouseX - LastMousePosition.X) * 0.2f;
		Look.Pitch = FMath::Clamp(Look.Pitch - (MouseY - LastMousePosition.Y) * 0.2f, -80.f, 80.f);
		Look.Roll = 0.f;
		Camera->SetRelativeRotation(Look);
	}
	bHadMousePosition = bHaveMouse;
	LastMousePosition = FVector2D(MouseX, MouseY);

	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		DesktopHoldDistance += 5.f;
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		DesktopHoldDistance -= 5.f;
	}
	DesktopHoldDistance = FMath::Clamp(DesktopHoldDistance, 25.f, 200.f);

	FVector Origin;
	FVector Direction;
	if (!PC->DeprojectMousePositionToWorld(Origin, Direction))
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyDesktopTrace), false, this);
	Params.AddIgnoredActor(LeftHand);
	Params.AddIgnoredActor(RightHand);
	if (AToyCube* Held = RightHand->GetHeldCube())
	{
		Params.AddIgnoredActor(Held);
	}
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 500.f, ECC_Visibility, Params);

	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		AToyTouchScreen* Screen = bHit ? Cast<AToyTouchScreen>(Hit.GetActor()) : nullptr;
		bDesktopClickIsPoke = Screen != nullptr;
		if (Screen)
		{
			Screen->Poke(Hit.GetComponent());
		}
	}
	const bool bLeftDown = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bLeftDown)
	{
		bDesktopClickIsPoke = false;
	}

	// 没拿东西时手贴着鼠标指到的表面；拿着东西时保持距离，用滚轮调
	FVector GripTarget = Origin + Direction * DesktopHoldDistance;
	if (!RightHand->IsHolding() && bHit && Hit.Distance < 250.f)
	{
		GripTarget = Hit.ImpactPoint;
		DesktopHoldDistance = FMath::Clamp(Hit.Distance, 25.f, 200.f);
	}
	// 手指沿视线方向、掌心朝下，像从上面抓东西
	RightHand->SetDesktopPose(GripTarget, Direction.Rotation() + FRotator(0.f, 0.f, 90.f));
	RightHand->SetInputs(bLeftDown && !bDesktopClickIsPoke ? 1.f : 0.f, 0.f);
}

void AToyBoxPawn::PlayHaptic(EControllerHand Hand, float Amplitude, float Duration)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!bVR || !PC)
	{
		return;
	}
	PC->SetHapticsByValue(1.f, Amplitude, Hand);

	FTimerHandle& Timer = (Hand == EControllerHand::Left) ? LeftHapticTimer : RightHapticTimer;
	TWeakObjectPtr<APlayerController> WeakPC(PC);
	GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [WeakPC, Hand]()
	{
		if (APlayerController* Player = WeakPC.Get())
		{
			Player->SetHapticsByValue(0.f, 0.f, Hand);
		}
	}), Duration, false);
}
