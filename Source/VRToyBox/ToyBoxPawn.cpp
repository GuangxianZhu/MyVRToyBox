#include "ToyBoxPawn.h"

#include "ToyCube.h"
#include "ToyHand.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MotionControllerComponent.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarHandPreview(
	TEXT("toy.HandPreview"),
	0,
	TEXT("桌面模式下把左手侧着摆在镜头前，检查手模型：1 = 张开，2 = 握拳，3 = 来回握"));

namespace
{
	const TCHAR* ArcMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* RingMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GlowMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr int32 ArcDots = 40;
	constexpr float Glow = 12.f;

	/** 把几个设备上的同一个摇杆轴取绝对值最大的（Quest / Index / WMR / Vive 的键名不同）。 */
	TArray<FKey> Keys(std::initializer_list<const TCHAR*> Names)
	{
		TArray<FKey> Out;
		for (const TCHAR* Name : Names)
		{
			Out.Add(FKey(Name));
		}
		return Out;
	}
}

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
	MoveXKeys = Keys({ TEXT("OculusTouch_Left_Thumbstick_X"), TEXT("ValveIndex_Left_Thumbstick_X"),
		TEXT("MixedReality_Left_Thumbstick_X"), TEXT("Vive_Left_Trackpad_X") });
	MoveYKeys = Keys({ TEXT("OculusTouch_Left_Thumbstick_Y"), TEXT("ValveIndex_Left_Thumbstick_Y"),
		TEXT("MixedReality_Left_Thumbstick_Y"), TEXT("Vive_Left_Trackpad_Y") });
	TeleportKeys = Keys({ TEXT("OculusTouch_Right_Thumbstick_Y"), TEXT("ValveIndex_Right_Thumbstick_Y"),
		TEXT("MixedReality_Right_Thumbstick_Y"), TEXT("Vive_Right_Trackpad_Y") });

	// 传送瞄准的显示：一串小球当抛物线，一个扁圆柱当落点圈。都只显示，不碰撞、不投影
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ArcMesh(ArcMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RingMesh(RingMeshPath);
	TeleportArc = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TeleportArc"));
	TeleportArc->SetupAttachment(VROrigin);
	TeleportArc->SetUsingAbsoluteLocation(true);
	TeleportArc->SetUsingAbsoluteRotation(true);
	TeleportArc->SetUsingAbsoluteScale(true);
	TeleportArc->SetStaticMesh(ArcMesh.Object);
	TeleportArc->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	TeleportArc->SetCastShadow(false);
	TeleportArc->SetHiddenInGame(true);
	TeleportRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeleportRing"));
	TeleportRing->SetupAttachment(VROrigin);
	TeleportRing->SetUsingAbsoluteLocation(true);
	TeleportRing->SetUsingAbsoluteRotation(true);
	TeleportRing->SetUsingAbsoluteScale(true);
	TeleportRing->SetStaticMesh(RingMesh.Object);
	TeleportRing->SetWorldScale3D(FVector(0.5f, 0.5f, 0.01f));
	TeleportRing->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	TeleportRing->SetCastShadow(false);
	TeleportRing->SetHiddenInGame(true);
}

void AToyBoxPawn::BeginPlay()
{
	Super::BeginPlay();
	LeftHand = SpawnHand(EControllerHand::Left, LeftController);
	RightHand = SpawnHand(EControllerHand::Right, RightController);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath))
	{
		TeleportMaterial = UMaterialInstanceDynamic::Create(Base, this);
		TeleportMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.6f);
		TeleportArc->SetMaterial(0, TeleportMaterial);
		TeleportRing->SetMaterial(0, TeleportMaterial);
	}
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
	else if (!bExternalControl)
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

	// 左摇杆：平滑移动
	SmoothMove(FVector2D(ReadAxis(MoveXKeys), ReadAxis(MoveYKeys)), GetWorld()->GetDeltaSeconds());

	// 右摇杆往前推：从右手打抛物线瞄准，摇杆回中时传送（UE VR 模板的做法）
	const float Aim = ReadAxis(TeleportKeys);
	const float Turn = ReadAxis(TurnKeys);
	if (Aim > 0.6f && FMath::Abs(Turn) < 0.6f)
	{
		bTeleportAiming = true;
		const FTransform Hand = RightController->GetComponentTransform();
		bTeleportValid = UpdateTeleportAim(Hand.GetLocation(), Hand.GetUnitAxis(EAxis::X));
		return;
	}
	if (bTeleportAiming && Aim < 0.3f)
	{
		bTeleportAiming = false;
		HideTeleportAim();
		if (bTeleportValid)
		{
			Teleport(TeleportTarget);
		}
		return;
	}
	if (bTeleportAiming)
	{
		return;
	}

	// 快速转身：摇杆推过 0.7 转一次，回到 0.3 以内才能再转
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

FVector AToyBoxPawn::GetFeetLocation() const
{
	// 头的正下方、VROrigin（地面）的高度
	const FVector Head = Camera->GetComponentLocation();
	return FVector(Head.X, Head.Y, GetActorLocation().Z);
}

void AToyBoxPawn::SmoothMove(FVector2D Input, float DeltaSeconds)
{
	const float Size = Input.Size();
	if (Size < MoveDeadZone || bTeleporting)
	{
		return;
	}
	// 死区以外重新映射到 0~1，推一点点就是慢慢走
	Input *= FMath::Min(1.f, (Size - MoveDeadZone) / (1.f - MoveDeadZone)) / Size;
	const FRotator Yaw(0.f, Camera->GetComponentRotation().Yaw, 0.f);
	const FVector Delta = (Yaw.RotateVector(FVector::ForwardVector) * Input.Y + Yaw.RotateVector(FVector::RightVector) * Input.X)
		* MoveSpeed * DeltaSeconds;

	// 胶囊体立在头的正下方，从现在的位置扫到目标位置；碰到墙 / 桌子就停在碰撞点前，再沿墙面滑一下
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyMove), false, this);
	for (AActor* Ignored : { static_cast<AActor*>(LeftHand), static_cast<AActor*>(RightHand) })
	{
		Params.AddIgnoredActor(Ignored);
	}
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	FVector Move = Delta;
	for (int32 Pass = 0; Pass < 2 && !Move.IsNearlyZero(); ++Pass)
	{
		const FVector Center = GetFeetLocation() + FVector(0.f, 0.f, CapsuleLift + CapsuleHalfHeight);
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Center, Center + Move, FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			AddActorWorldOffset(Move);
			return;
		}
		if (Hit.bStartPenetrating)
		{
			return;
		}
		AddActorWorldOffset(Move * Hit.Time);
		// 剩下的位移沿墙面投影，贴着墙走
		const FVector Normal = FVector(Hit.Normal.X, Hit.Normal.Y, 0.f).GetSafeNormal();
		Move = Move * (1.f - Hit.Time);
		Move -= Normal * FVector::DotProduct(Move, Normal);
	}
}

bool AToyBoxPawn::IsTeleportTargetValid(const FHitResult& Hit) const
{
	// 只能落在工作室的地板上（地毯也算地板），房间以内，脚下那块地方站得下一个胶囊体（不在桌子底下、不贴墙）
	const AToyStudio* Studio = Cast<AToyStudio>(Hit.GetActor());
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Studio || !Component || Hit.ImpactNormal.Z < 0.8f)
	{
		return false;
	}
	const FString Name = Component->GetName();
	if (!Name.StartsWith(TEXT("Floor")) && !Name.StartsWith(TEXT("Rug")))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTeleport), false, this);
	Params.AddIgnoredActor(LeftHand);
	Params.AddIgnoredActor(RightHand);
	const FVector Center = Hit.ImpactPoint + FVector(0.f, 0.f, CapsuleLift + CapsuleHalfHeight);
	return !GetWorld()->OverlapAnyTestByChannel(Center, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), Params);
}

bool AToyBoxPawn::UpdateTeleportAim(const FVector& Start, const FVector& Direction)
{
	FPredictProjectilePathParams Path(1.f, Start, Direction.GetSafeNormal() * TeleportLaunchSpeed, 2.f, ECC_Visibility, this);
	Path.bTraceWithCollision = true;
	Path.bTraceComplex = false;
	Path.SimFrequency = 20.f;
	Path.ActorsToIgnore = { LeftHand, RightHand };
	if (LeftHand && LeftHand->GetHeldActor()) Path.ActorsToIgnore.Add(LeftHand->GetHeldActor());
	if (RightHand && RightHand->GetHeldActor()) Path.ActorsToIgnore.Add(RightHand->GetHeldActor());
	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(GetWorld(), Path, Result);
	const bool bValid = bHit && IsTeleportTargetValid(Result.HitResult);

	// 抛物线：沿路径均匀摆 ArcDots 个小球
	TeleportArc->ClearInstances();
	const TArray<FPredictProjectilePathPointData>& Points = Result.PathData;
	if (Points.Num() >= 2)
	{
		for (int32 Index = 0; Index < ArcDots; ++Index)
		{
			const float T = static_cast<float>(Index) / (ArcDots - 1) * (Points.Num() - 1);
			const int32 I0 = FMath::Min(FMath::FloorToInt(T), Points.Num() - 2);
			const FVector Location = FMath::Lerp(Points[I0].Location, Points[I0 + 1].Location, T - I0);
			TeleportArc->AddInstance(FTransform(FQuat::Identity, Location, FVector(0.025f)), true);
		}
	}
	if (TeleportMaterial)
	{
		const FLinearColor Color = bValid ? FLinearColor(0.2f, 0.85f, 1.f) : FLinearColor(1.f, 0.15f, 0.1f);
		TeleportMaterial->SetVectorParameterValue(TEXT("Color"), Color * Glow);
	}
	TeleportArc->SetHiddenInGame(false);
	TeleportRing->SetHiddenInGame(!bHit);
	if (bHit)
	{
		TeleportRing->SetWorldLocation(Result.HitResult.ImpactPoint + FVector(0.f, 0.f, 0.6f));
		TeleportTarget = Result.HitResult.ImpactPoint;
	}
	return bValid;
}

void AToyBoxPawn::HideTeleportAim()
{
	TeleportArc->ClearInstances();
	TeleportArc->SetHiddenInGame(true);
	TeleportRing->SetHiddenInGame(true);
}

void AToyBoxPawn::Teleport(const FVector& Target)
{
	if (bTeleporting)
	{
		return;
	}
	bTeleporting = true;
	APlayerController* PC = Cast<APlayerController>(GetController());
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (CameraManager)
	{
		CameraManager->StartCameraFade(0.f, 1.f, TeleportFadeTime, FLinearColor::Black, false, true);
	}
	TWeakObjectPtr<APlayerCameraManager> WeakCamera(CameraManager);
	GetWorldTimerManager().SetTimer(TeleportTimer, FTimerDelegate::CreateWeakLambda(this, [this, Target, WeakCamera]()
	{
		// 让头落在落点正上方：VROrigin 平移 = 落点 - 现在脚下的点
		AddActorWorldOffset(Target - GetFeetLocation());
		UE_LOG(LogTemp, Log, TEXT("ToyBox: 传送到 %s"), *Target.ToCompactString());
		if (APlayerCameraManager* Manager = WeakCamera.Get())
		{
			Manager->StartCameraFade(1.f, 0.f, TeleportFadeTime, FLinearColor::Black, false, false);
		}
		bTeleporting = false;
	}), FMath::Max(0.01f, TeleportFadeTime), false);
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

	// 复杂碰撞（按三角形）：电机零件只有网格本身挡 Visibility，挨得很近的零件也能点准
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyDesktopTrace), true, this);
	Params.AddIgnoredActor(LeftHand);
	Params.AddIgnoredActor(RightHand);
	if (AActor* Held = RightHand->GetHeldActor())
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
	// 鼠标点中的零件优先：它的检测盒可能和旁边零件的盒子重叠
	RightHand->SetPreferredTarget(bHit ? Hit.GetActor() : nullptr);
	// 手指沿视线方向、掌心朝下，像从上面抓东西
	// 只取视线的水平朝向（不跟着低头），Roll -90 让掌心真的朝下（+90 是掌心朝上，抓零件时零件会翻到手掌上面去）
	RightHand->SetDesktopPose(GripTarget, FRotator(0.f, Direction.Rotation().Yaw, -90.f));
	RightHand->SetInputs(0.f, bLeftDown && !bDesktopClickIsPoke ? 1.f : 0.f);

	// WASD 平滑移动（和 VR 左摇杆同一套），按住 T 从镜头沿鼠标方向瞄准传送，松开 T 传送
	const FVector2D Keys((PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f),
		(PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f));
	SmoothMove(Keys, GetWorld()->GetDeltaSeconds());
	if (PC->IsInputKeyDown(EKeys::T))
	{
		bTeleportAiming = true;
		bTeleportValid = UpdateTeleportAim(Origin + Direction * 30.f, (Direction + FVector(0.f, 0.f, 0.25f)).GetSafeNormal());
	}
	else if (bTeleportAiming)
	{
		bTeleportAiming = false;
		HideTeleportAim();
		if (bTeleportValid)
		{
			Teleport(TeleportTarget);
		}
	}

	// 调试：左手平时在桌面模式下藏着，toy.HandPreview 打开时摆到镜头右前方、侧面朝镜头，方便看手指怎么弯
	const int32 Preview = CVarHandPreview.GetValueOnGameThread();
	LeftHand->SetActorHiddenInGame(Preview == 0);
	LeftHand->SetActorTickEnabled(Preview != 0);
	if (Preview != 0)
	{
		const FTransform View = Camera->GetComponentTransform();
		const FVector Location = View.GetLocation() + View.GetUnitAxis(EAxis::X) * 35.f + View.GetUnitAxis(EAxis::Y) * 8.f;
		LeftHand->SetActorLocationAndRotation(Location,
			FRotationMatrix::MakeFromXZ(View.GetUnitAxis(EAxis::Y), View.GetUnitAxis(EAxis::Z)).Rotator());
		const float Curl = Preview == 1 ? 0.f : (Preview == 2 ? 1.f : 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 2.f));
		LeftHand->SetInputs(Curl, Curl);
	}
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
