#include "ToyBoxPawn.h"

#include "ToyCube.h"
#include "ToyHand.h"
#include "ToyMenuPanel.h"
#include "ToySettings.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	FAutoConsoleCommandWithWorldAndArgs HapticTestCommand(TEXT("toy.HapticTest"),
		TEXT("在右手上放一串测试震动，右手边的信息牌显示步骤：1 = 振幅阶梯，2 = 时长阶梯，3 = 点击 vs 包络，4 = 齿轮"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr)
			{
				Pawn->StartHapticTest(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs LookCommand(TEXT("toy.Look"),
		TEXT("把镜头转到指定朝向（相对玩家开局的朝向，度）：toy.Look <Yaw> [Pitch，默认 -40]。测帧率、截图时用，比如 toy.Look 180 -10 看身后。")
		TEXT("VR 里头的朝向由头显决定，只转身（Yaw），Pitch 不管"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
			if (!Pawn || Args.Num() == 0)
			{
				return;
			}
			const float Yaw = FCString::Atof(*Args[0]);
			if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
			{
				static TMap<TWeakObjectPtr<AToyBoxPawn>, float> StartYaw;
				const float Start = StartYaw.FindOrAdd(Pawn, Pawn->GetActorRotation().Yaw);
				Pawn->SnapTurn(FRotator::NormalizeAxis(Start + Yaw - Pawn->GetActorRotation().Yaw));
				return;
			}
			Pawn->Camera->SetRelativeRotation(FRotator(Args.Num() > 1 ? FCString::Atof(*Args[1]) : -40.f, Yaw, 0.f));
		}));

	FAutoConsoleCommandWithWorldAndArgs StandCommand(TEXT("toy.Stand"),
		TEXT("把玩家挪到工作室坐标 (X, Y)（厘米，原点是开局站立点，X 朝桌子），身体朝向和工作室一致：toy.Stand <X> <Y>。")
		TEXT("测帧率时站到别处用，比如 toy.Stand 160 0 再 toy.Look 180 -10 = 绕到桌子后面看客厅"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
			AToyStudio* Studio = AToyStudio::Find(World);
			if (!Pawn || !Studio || Args.Num() < 2)
			{
				return;
			}
			const FTransform StudioTransform = Studio->GetActorTransform();
			FVector Target = StudioTransform.TransformPosition(FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 0.f));
			Target.Z = Pawn->GetActorLocation().Z;
			Pawn->SetActorLocationAndRotation(Target, FRotator(0.f, StudioTransform.Rotator().Yaw, 0.f));
		}));

	FAutoConsoleCommandWithWorldAndArgs WalkTestCommand(TEXT("toy.WalkTest"),
		TEXT("从工作室坐标 (X0, Y0) 用摇杆走路的碰撞走到 (X1, Y1)，再沿线检查每个点能不能传送，挡路的东西都写进日志：toy.WalkTest <X0> <Y0> <X1> <Y1>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
			if (Pawn && Args.Num() >= 4)
			{
				Pawn->RunWalkTest(FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])),
					FVector2D(FCString::Atof(*Args[2]), FCString::Atof(*Args[3])));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs AfterCommand(TEXT("toy.After"),
		TEXT("过几秒再执行一条控制台命令：toy.After <秒> <命令...>。-ExecCmds 里的命令都在开局一起执行，测帧率要等着色器、动画稳定再测"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 2)
			{
				return;
			}
			const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
			TWeakObjectPtr<UWorld> WeakWorld = World;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Command](float)
			{
				// 走玩家控制器，和在控制台里敲一样（shot、stat 这些要视口处理的命令 GEngine->Exec 不认）。
				// 中间穿门换了关卡的话，原来的世界已经没了，用现在视口里的世界
				UWorld* Current = WeakWorld.IsValid() ? WeakWorld.Get() : (GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr);
				if (APlayerController* PC = Current ? Current->GetFirstPlayerController() : nullptr)
				{
					PC->ConsoleCommand(Command);
				}
				return false;
			}), FCString::Atof(*Args[0]));
		}));

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

	// 瞄准 / 掌心姿态：瞄准姿态给传送抛物线用；掌心姿态只做诊断（没协商到 OpenXR 1.1 / XR_EXT_palm_pose 时会悄悄退回握把姿态）
	auto MakeSource = [this](const TCHAR* Name)
	{
		UMotionControllerComponent* Source = CreateDefaultSubobject<UMotionControllerComponent>(Name);
		Source->SetupAttachment(VROrigin);
		Source->SetTrackingMotionSource(FName(Name));
		return Source;
	};
	LeftAim = MakeSource(TEXT("LeftAim"));
	RightAim = MakeSource(TEXT("RightAim"));
	LeftPalm = MakeSource(TEXT("LeftPalm"));
	RightPalm = MakeSource(TEXT("RightPalm"));

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
	// 手部校准：长按左摇杆进出，A 切换页，B 恢复默认（这几个键别处都没用）
	CalibToggleKeys = Keys({ TEXT("OculusTouch_Left_Thumbstick_Click"), TEXT("ValveIndex_Left_Thumbstick_Click"),
		TEXT("MixedReality_Left_Thumbstick_Click") });
	CalibPageKeys = Keys({ TEXT("OculusTouch_Right_A_Click"), TEXT("ValveIndex_Right_A_Click") });
	CalibResetKeys = Keys({ TEXT("OculusTouch_Right_B_Click"), TEXT("ValveIndex_Right_B_Click") });

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
	ToyHandCalib::Load();
	ToySettings::Load();
	LeftHand = SpawnHand(EControllerHand::Left, LeftController);
	RightHand = SpawnHand(EControllerHand::Right, RightController);
	LogHandSelfCheck();
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
	TickHaptics();
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
		bCalibApplied = false;
		ApplyHandCalib();
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

	// 手部校准参数变了（校准模式、控制台 toy.HandPitch 等）就重新套
	if (!bCalibApplied || !(ToyHandCalib::Get() == AppliedCalib))
	{
		ApplyHandCalib();
	}
	LogPoseDiagnostics();

	// 长按左摇杆 1 秒：进 / 出校准模式（按住不放只切一次）
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	if (ReadAxis(CalibToggleKeys) > 0.5f)
	{
		CalibHoldTime += DeltaSeconds;
		if (bCalibToggleArmed && CalibHoldTime >= 1.f)
		{
			bCalibToggleArmed = false;
			ToggleHandCalibMode();
		}
	}
	else
	{
		CalibHoldTime = 0.f;
		bCalibToggleArmed = true;
	}
	if (bCalibMode)
	{
		// 校准模式里摇杆都拿去调参数：暂停移动、转身、传送
		TickHandCalibMode(DeltaSeconds);
		return;
	}

	// 左摇杆：平滑移动
	SmoothMove(FVector2D(ReadAxis(MoveXKeys), ReadAxis(MoveYKeys)), GetWorld()->GetDeltaSeconds());

	// 右摇杆往前推：从右手打抛物线瞄准，摇杆回中时传送（UE VR 模板的做法）
	// 设置里关了传送：右摇杆往前推不再瞄准
	const float Aim = ToySettings::Get().bTeleport ? ReadAxis(TeleportKeys) : 0.f;
	const float Turn = ReadAxis(TurnKeys);
	if (Aim > 0.6f && FMath::Abs(Turn) < 0.6f)
	{
		bTeleportAiming = true;
		// 从右手的瞄准姿态（手柄实际指的方向）发出；握把姿态的 +X 沿着握把，比实际指向高约 60°
		const FTransform Hand = (RightAim->IsTracked() ? RightAim : RightController)->GetComponentTransform();
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

	// 平滑转身（设置里选 SMOOTH）：死区 0.2 以外按推的多少连续转
	if (ToySettings::Get().Turn == ToySettings::ETurn::Smooth)
	{
		if (FMath::Abs(Turn) > 0.2f)
		{
			const float Amount = FMath::Sign(Turn) * (FMath::Abs(Turn) - 0.2f) / 0.8f;
			SnapTurn(Amount * SmoothTurnSpeed * GetWorld()->GetDeltaSeconds());
		}
		return;
	}

	// 分段转身：摇杆推过 0.7 转一次（45° 或 30°，设置里选），回到 0.3 以内才能再转
	const float SnapDegrees = ToySettings::SnapTurnDegrees();
	if (bSnapTurnArmed && FMath::Abs(Turn) > 0.7f)
	{
		SnapTurn(Turn > 0.f ? SnapDegrees : -SnapDegrees);
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

void AToyBoxPawn::AddIgnoredForMove(FCollisionQueryParams& Params) const
{
	// 手和手里拿着的东西不挡路（拿着方块走路时，方块不该把自己卡住）
	for (const AToyHand* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		if (Hand)
		{
			Params.AddIgnoredActor(Hand);
			if (const AActor* Held = Hand->GetHeldActor())
			{
				Params.AddIgnoredActor(Held);
			}
		}
	}
}

void AToyBoxPawn::ApplyHandCalib()
{
	AppliedCalib = ToyHandCalib::Get();
	bCalibApplied = true;
	if (!bVR)
	{
		return;   // 桌面模式手不挂在控制器上，姿态由 TickDesktop 摆，不受校准影响
	}
	LeftHand->SetActorRelativeTransform(ToyHandCalib::HandTransform(EControllerHand::Left, AppliedCalib));
	RightHand->SetActorRelativeTransform(ToyHandCalib::HandTransform(EControllerHand::Right, AppliedCalib));
}

void AToyBoxPawn::LogHandSelfCheck() const
{
	// 桌面就能做的自检：手腕 → 中指根的方向（手坐标系）套上 VR 校准变换，就是 VR 里它在握把系里的方向。
	// Pitch -64 时应该约 (0.44, 0, -0.90)：手指往前下方伸，像握着手柄时的样子
	for (const AToyHand* Hand : { RightHand.Get(), LeftHand.Get() })
	{
		if (!Hand)
		{
			continue;
		}
		const EControllerHand Side = Hand == LeftHand.Get() ? EControllerHand::Left : EControllerHand::Right;
		const FVector Local = Hand->GetFingerDirectionLocal();
		const FVector InGrip = ToyHandCalib::HandTransform(Side).TransformVectorNoScale(Local);
		UE_LOG(LogTemp, Log, TEXT("ToyBox HandCalib 自检：%s手 手腕→中指根 手坐标 (%.2f, %.2f, %.2f) → 握把系 (%.2f, %.2f, %.2f)（%s）"),
			Side == EControllerHand::Left ? TEXT("左") : TEXT("右"), Local.X, Local.Y, Local.Z, InGrip.X, InGrip.Y, InGrip.Z,
			*ToyHandCalib::Describe());
	}
}

void AToyBoxPawn::LogPoseDiagnostics()
{
	// 追踪生效后打一次：瞄准 / 掌心姿态相对握把姿态。期望 Aim ≈ Pitch -60、(7.35, 0, -6.73)；
	// Palm ≈ Pitch -70、(-0.75, ±1, 1.3)。Palm 和 Grip 完全一样 = 运行时没给掌心姿态，退回了握把姿态
	if (bPoseDiagnosticsLogged || !RightController->IsTracked() || !RightAim->IsTracked())
	{
		return;
	}
	bPoseDiagnosticsLogged = true;
	const UMotionControllerComponent* Sets[2][3] = { { RightController, RightAim, RightPalm }, { LeftController, LeftAim, LeftPalm } };
	for (const auto& Set : Sets)
	{
		const FTransform Grip = Set[0]->GetComponentTransform();
		for (int32 Index = 1; Index < 3; ++Index)
		{
			const FTransform Relative = Set[Index]->GetComponentTransform().GetRelativeTransform(Grip);
			UE_LOG(LogTemp, Log, TEXT("ToyBox 姿态诊断：%s 相对 %s：Rot %s  Loc %s  %s"),
				*Set[Index]->MotionSource.ToString(), *Set[0]->MotionSource.ToString(), *Relative.Rotator().ToCompactString(),
				*Relative.GetLocation().ToCompactString(), Set[Index]->IsTracked() ? TEXT("") : TEXT("（没在追踪）"));
		}
	}
}

bool AToyBoxPawn::EnterHandCalibMode()
{
	if (!bVR || bCalibMode)
	{
		return false;
	}
	ToggleHandCalibMode();
	return true;
}

void AToyBoxPawn::ToggleHandCalibMode()
{
	bCalibMode = !bCalibMode;
	if (bCalibMode)
	{
		bTeleportAiming = false;
		HideTeleportAim();
		bCalibPositionPage = false;
		const ToyHandCalib::FParams Params = ToyHandCalib::Get();
		CalibRotStep = FIntVector(FMath::FloorToInt(Params.Pitch), FMath::FloorToInt(Params.Yaw), FMath::FloorToInt(Params.Roll));
		CalibPosStep = FIntVector(FMath::FloorToInt(Params.Offset.X * 2.f), FMath::FloorToInt(Params.Offset.Y * 2.f), FMath::FloorToInt(Params.Offset.Z * 2.f));
		PlayHaptic(EControllerHand::Left, 0.8f, 0.15f);
		UE_LOG(LogTemp, Log, TEXT("ToyBox: 进入手部校准模式"));
	}
	else
	{
		ToyHandCalib::Save();
		RightHand->HideCustomInfo();
		PlayHaptic(EControllerHand::Left, 0.8f, 0.15f);
		UE_LOG(LogTemp, Log, TEXT("ToyBox: 退出手部校准模式，已保存"));
	}
}

void AToyBoxPawn::TickHandCalibMode(float DeltaSeconds)
{
	// 死区 0.2，死区外重新映射到 0~1
	auto Stick = [](float Value)
	{
		const float Size = FMath::Abs(Value);
		return Size < 0.2f ? 0.f : FMath::Sign(Value) * (Size - 0.2f) / 0.8f;
	};
	const float RX = Stick(ReadAxis(TurnKeys));
	const float RY = Stick(ReadAxis(TeleportKeys));
	const float LX = Stick(ReadAxis(MoveXKeys));
	const float LY = Stick(ReadAxis(MoveYKeys));

	ToyHandCalib::FParams Params = ToyHandCalib::Get();
	const bool bPageDown = ReadAxis(CalibPageKeys) > 0.5f;
	if (bPageDown && !bCalibPageWasDown)
	{
		bCalibPositionPage = !bCalibPositionPage;
		PlayHaptic(EControllerHand::Right, 0.6f, 0.08f);
	}
	bCalibPageWasDown = bPageDown;
	const bool bResetDown = ReadAxis(CalibResetKeys) > 0.5f;
	if (bResetDown && !bCalibResetWasDown)
	{
		Params = ToyHandCalib::FParams();
		PlayHaptic(EControllerHand::Right, 1.f, 0.2f);
	}
	bCalibResetWasDown = bResetDown;

	if (!bCalibPositionPage)
	{
		// 旋转页：右摇杆上下 = Pitch，左右 = Yaw，左摇杆左右 = Roll，30°/秒
		Params.Pitch += RY * 30.f * DeltaSeconds;
		Params.Yaw += RX * 30.f * DeltaSeconds;
		Params.Roll += LX * 30.f * DeltaSeconds;
	}
	else
	{
		// 位置页：右摇杆上下 = OffsetX，左右 = OffsetY，左摇杆上下 = OffsetZ，2cm/秒
		Params.Offset.X += RY * 2.f * DeltaSeconds;
		Params.Offset.Y += RX * 2.f * DeltaSeconds;
		Params.Offset.Z += LY * 2.f * DeltaSeconds;
	}
	ToyHandCalib::Set(Params);

	// 每过 1° 或 0.5cm 震一下，不看数字也知道在变
	const FIntVector Rot(FMath::FloorToInt(Params.Pitch), FMath::FloorToInt(Params.Yaw), FMath::FloorToInt(Params.Roll));
	const FIntVector Pos(FMath::FloorToInt(Params.Offset.X * 2.f), FMath::FloorToInt(Params.Offset.Y * 2.f), FMath::FloorToInt(Params.Offset.Z * 2.f));
	if (Rot != CalibRotStep || Pos != CalibPosStep)
	{
		CalibRotStep = Rot;
		CalibPosStep = Pos;
		PlayHaptic(EControllerHand::Right, 0.25f, 0.02f);
	}

	const FText Title = FText::FromString(bCalibPositionPage ? TEXT("手部校准 · 位置") : TEXT("手部校准 · 角度"));
	const FText Body = FText::FromString(FString::Printf(
		TEXT("P %.1f  Y %.1f  R %.1f  |  X %.1f  Y %.1f  Z %.1f\n%s\nA 切换角度 / 位置，B 恢复默认，长按左摇杆退出并保存"),
		Params.Pitch, Params.Yaw, Params.Roll, Params.Offset.X, Params.Offset.Y, Params.Offset.Z,
		bCalibPositionPage ? TEXT("右摇杆上下 = X（前后），左右 = Y（左右），左摇杆上下 = Z（上下），单位 cm")
			: TEXT("右摇杆上下 = Pitch，左右 = Yaw，左摇杆左右 = Roll，单位度")));
	RightHand->ShowCustomInfo(Title, Body);
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
		* ToySettings::MoveSpeedCm() * DeltaSeconds;

	MoveWithCollision(Delta);
}

bool AToyBoxPawn::MoveWithCollision(FVector Move, FHitResult* OutHit)
{
	// 胶囊体立在头的正下方，从现在的位置扫到目标位置；碰到墙 / 桌子就停在碰撞点前，再沿墙面滑一下
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyMove), false, this);
	AddIgnoredForMove(Params);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	// 碰到东西时停在碰撞点前这么远，不贴着。贴着停的话下一帧扫掠一开始就"嵌在里面"，以前那种情况只许往外退、不许沿墙滑，
	// 走到门框边上就再也挪不动了（门口卡住就是这个）
	constexpr float Skin = 0.5f;
	const FVector Requested = Move;
	const FVector Start = GetActorLocation();
	bool bBlocked = false;
	for (int32 Pass = 0; Pass < 3 && !Move.IsNearlyZero(); ++Pass)
	{
		const FVector Center = GetFeetLocation() + FVector(0.f, 0.f, CapsuleLift + CapsuleHalfHeight);
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Center, Center + Move, FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			AddActorWorldOffset(Move);
			Move = FVector::ZeroVector;
			break;
		}
		if (OutHit && !bBlocked)
		{
			*OutHit = Hit;
		}
		bBlocked = true;
		const FVector Normal = FVector(Hit.Normal.X, Hit.Normal.Y, 0.f).GetSafeNormal();
		if (Hit.bStartPenetrating)
		{
			// 一开始就嵌在东西里（真人走进了桌子、或者上一帧贴着停）：先水平推出来，再把往里的那部分去掉、沿表面滑
			if (!Normal.IsNearlyZero())
			{
				AddActorWorldOffset(Normal * (FMath::Min(Hit.PenetrationDepth, 5.f) + Skin));
				Move -= Normal * FMath::Min(0.f, FVector::DotProduct(Move, Normal));
			}
			else
			{
				break;
			}
			continue;
		}
		const float MoveSize = Move.Size();
		AddActorWorldOffset(Move * FMath::Max(0.f, Hit.Time - Skin / FMath::Max(MoveSize, KINDA_SMALL_NUMBER)));
		// 剩下的位移沿墙面投影，贴着墙走
		Move = Move * (1.f - Hit.Time);
		Move -= Normal * FVector::DotProduct(Move, Normal);
	}
	// 门框角上：正对着角走，沿两个面滑都几乎不往前。往左右各偏 20°~70° 试一下，哪个方向能整步走过去就走哪个，
	// 人往门口走、稍微偏一点也能被"引"进门洞
	const FVector Progress = GetActorLocation() - Start;
	if (bBlocked && FVector::DotProduct(Progress, Requested.GetSafeNormal()) < 0.3f * Requested.Size())
	{
		const FVector Center = GetFeetLocation() + FVector(0.f, 0.f, CapsuleLift + CapsuleHalfHeight);
		for (const float Angle : { 20.f, -20.f, 40.f, -40.f, 70.f, -70.f })
		{
			const FVector Try = Requested.RotateAngleAxis(Angle, FVector::UpVector);
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByChannel(Hit, Center, Center + Try, FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				AddActorWorldOffset(Try);
				break;
			}
		}
	}
	return bBlocked;
}

void AToyBoxPawn::RunWalkTest(FVector2D From, FVector2D To)
{
	const AToyStudio* Studio = AToyStudio::Find(GetWorld());
	if (!Studio)
	{
		return;
	}
	const FTransform StudioTransform = Studio->GetActorTransform();
	auto World3 = [&](FVector2D P, float Z) { FVector V = StudioTransform.TransformPosition(FVector(P.X, P.Y, 0.f)); V.Z = Z; return V; };
	// 头挪到起点正上方（桌面模式头在 VROrigin 上方，VR 里头可能偏开一点）
	const FVector HeadOffset = Camera->GetComponentLocation() - GetActorLocation();
	SetActorLocation(World3(From, GetActorLocation().Z) - FVector(HeadOffset.X, HeadOffset.Y, 0.f));
	const FVector Goal = World3(To, GetActorLocation().Z);
	auto Describe = [](const FHitResult& Hit)
	{
		const UPrimitiveComponent* C = Hit.GetComponent();
		const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
		return FString::Printf(TEXT("%s.%s [%s] 碰撞点 (%.0f, %.0f, %.0f) 法线 (%.2f, %.2f, %.2f)%s"),
			Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("?"), C ? *C->GetName() : TEXT("?"),
			SM && SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : TEXT("-"),
			Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z, Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z,
			Hit.bStartPenetrating ? TEXT("（一开始就嵌在里面）") : TEXT(""));
	};
	UE_LOG(LogTemp, Display, TEXT("WalkTest: (%.0f, %.0f) → (%.0f, %.0f)（工作室坐标）"), From.X, From.Y, To.X, To.Y);
	// 摇杆走路：每步 3cm（90 帧 / 秒、走路速度的量级），走不动了就停
	int32 Stuck = 0;
	FString LastHit;
	for (int32 Step = 0; Step < 2000 && Stuck < 10; ++Step)
	{
		FVector Feet = GetFeetLocation();
		FVector ToGoal = Goal - Feet;
		ToGoal.Z = 0.f;
		if (ToGoal.Size() < 3.f)
		{
			break;
		}
		const FVector Before = Feet;
		FHitResult Hit;
		if (MoveWithCollision(ToGoal.GetClampedToMaxSize(3.f), &Hit))
		{
			const FString Text = Describe(Hit);
			if (Text != LastHit)
			{
				const FVector L = StudioTransform.InverseTransformPosition(Before);
				UE_LOG(LogTemp, Display, TEXT("WalkTest:   在 (%.0f, %.0f) 碰到 %s"), L.X, L.Y, *Text);
				LastHit = Text;
			}
		}
		Stuck = FVector::Dist2D(Before, GetFeetLocation()) < 0.5f ? Stuck + 1 : 0;
	}
	const FVector End = StudioTransform.InverseTransformPosition(GetFeetLocation());
	const bool bArrived = FVector::Dist2D(GetFeetLocation(), Goal) < 5.f;
	UE_LOG(LogTemp, Display, TEXT("WalkTest:   走路%s，停在 (%.0f, %.0f)"), bArrived ? TEXT("到了") : TEXT("卡住了"), End.X, End.Y);

	// 传送：沿线每 10cm 从头顶往下打（和抛物线一样用 Visibility、简单碰撞），看落点能不能传送；再从眼睛高度平着打一条，看有没有挡住抛物线的
	int32 Bad = 0;
	FString LastBad;
	const float Length = FVector2D::Distance(From, To);
	for (float D = 0.f; D <= Length; D += 10.f)
	{
		const FVector2D P = From + (To - From) * (D / FMath::Max(Length, 1.f));
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyWalkTest), false, this);
		AddIgnoredForMove(Params);
		const float Z = StudioTransform.GetLocation().Z;
		FString Why;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, World3(P, Z + 240.f), World3(P, Z - 50.f), ECC_Visibility, Params))
		{
			Why = TEXT("往下什么都没打到");
		}
		else if (!Studio->IsFloorHit(Hit))
		{
			Why = FString::Printf(TEXT("打到的不算地板：%s"), *Describe(Hit));
		}
		else if (!IsTeleportTargetValid(Hit))
		{
			Why = FString::Printf(TEXT("站不下胶囊体（落在 %s）"), *Describe(Hit));
		}
		if (!Why.IsEmpty())
		{
			++Bad;
			if (Why != LastBad)
			{
				UE_LOG(LogTemp, Display, TEXT("WalkTest:   传送 (%.0f, %.0f) 不行：%s"), P.X, P.Y, *Why);
				LastBad = Why;
			}
		}
	}
	FHitResult Eye;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyWalkTest), false, this);
	AddIgnoredForMove(Params);
	const float EyeZ = StudioTransform.GetLocation().Z + 150.f;
	if (GetWorld()->LineTraceSingleByChannel(Eye, World3(From, EyeZ), World3(To, EyeZ), ECC_Visibility, Params))
	{
		UE_LOG(LogTemp, Display, TEXT("WalkTest:   眼睛高度平着看过去被挡：%s"), *Describe(Eye));
	}
	UE_LOG(LogTemp, Display, TEXT("WalkTest:   传送：沿线 %d 个点里 %d 个不行"), FMath::FloorToInt(Length / 10.f) + 1, Bad);
}

bool AToyBoxPawn::IsTeleportTargetValid(const FHitResult& Hit) const
{
	// 只能落在工作室的地板上（地毯也算地板；套用样板间时是样板间的地面），房间以内，脚下那块地方站得下一个胶囊体（不在桌子底下、不贴墙）
	const AToyStudio* Studio = AToyStudio::Find(GetWorld());
	if (!Studio || !Studio->IsFloorHit(Hit))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTeleport), false, this);
	AddIgnoredForMove(Params);
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
	if (bValid)
	{
		TeleportRing->SetHiddenInGame(false);
		TeleportRing->SetWorldLocation(Result.HitResult.ImpactPoint + FVector(0.f, 0.f, 0.6f));
		TeleportTarget = Result.HitResult.ImpactPoint;
		return true;
	}
	// 落点无效（打到桌面、零件、墙上）：红圈贴到正下方的地上；下面找不到地就不显示圈
	bool bRingOnFloor = false;
	if (bHit)
	{
		FHitResult Floor;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(ToyTeleportFloor), false, this);
		AddIgnoredForMove(FloorParams);
		const FVector From = Result.HitResult.ImpactPoint + FVector(0.f, 0.f, 1.f);
		// 套用样板间时，样板间的地面也算地
		const AToyStudio* Studio = AToyStudio::Find(GetWorld());
		if (GetWorld()->LineTraceSingleByChannel(Floor, From, From - FVector(0.f, 0.f, 500.f), ECC_Visibility, FloorParams)
			&& Floor.ImpactNormal.Z > 0.8f && (Cast<AToyStudio>(Floor.GetActor()) || (Studio && !Studio->bBuildRoom)))
		{
			TeleportRing->SetWorldLocation(Floor.ImpactPoint + FVector(0.f, 0.f, 0.6f));
			bRingOnFloor = true;
		}
	}
	TeleportRing->SetHiddenInGame(!bRingOnFloor);
	return false;
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

	// 触屏是 GUI：鼠标指着它时每帧把打到的点交给它（按钮悬停变亮），按下左键就点那个按钮
	const bool bClick = PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	AToyTouchScreen* Screen = bHit ? Cast<AToyTouchScreen>(Hit.GetActor()) : nullptr;
	if (AToyTouchScreen* Previous = HoverScreen.Get(); Previous && Previous != Screen)
	{
		Previous->MouseLeave();
	}
	HoverScreen = Screen;
	if (Screen)
	{
		Screen->MouseAt(Hit.ImpactPoint, bClick);
	}
	if (bClick)
	{
		AToyMenuPanel* Menu = bHit ? Cast<AToyMenuPanel>(Hit.GetActor()) : nullptr;
		bDesktopClickIsPoke = Screen != nullptr || Menu != nullptr;
		if (Menu)
		{
			Menu->Poke(Hit.GetComponent());
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

	// M = 叫出 / 收起主菜单（VR 里戳触屏右上角的 MENU）
	if (PC->WasInputKeyJustPressed(EKeys::M))
	{
		for (TActorIterator<AToyMenuPanel> It(GetWorld()); It; ++It)
		{
			It->Toggle();
			break;
		}
	}

	// WASD 平滑移动（和 VR 左摇杆同一套），按住 T 从镜头沿鼠标方向瞄准传送，松开 T 传送
	const FVector2D Keys((PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f),
		(PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f));
	SmoothMove(Keys, GetWorld()->GetDeltaSeconds());
	if (PC->IsInputKeyDown(EKeys::T) && ToySettings::Get().bTeleport)
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
	FToyHapticLayer Layer;
	Layer.Amplitude = Amplitude;
	Layer.Hold = Duration;
	AddHaptic(Hand, Layer);
}

void AToyBoxPawn::AddHaptic(EControllerHand Hand, const FToyHapticLayer& Layer)
{
	FToyHapticLayer& Added = HapticLayers[Hand == EControllerHand::Left ? 0 : 1].Add_GetRef(Layer);
	Added.Start = GetWorld()->GetRealTimeSeconds() + Layer.Delay;
	Added.bEmitted = false;
}

void AToyBoxPawn::SetHapticDrive(EControllerHand Hand, float Amplitude)
{
	float& Drive = HapticDrive[Hand == EControllerHand::Left ? 0 : 1];
	Drive = FMath::Max(Drive, Amplitude);
}

void AToyBoxPawn::TickHaptics()
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	APlayerController* PC = Cast<APlayerController>(GetController());
	for (int32 Side = 0; Side < 2; ++Side)
	{
		float Sum = HapticDrive[Side];
		HapticDrive[Side] = 0.f;
		TArray<FToyHapticLayer>& Stack = HapticLayers[Side];
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			FToyHapticLayer& Layer = Stack[Index];
			if (Now < Layer.Start)
			{
				continue;
			}
			const float T = static_cast<float>(Now - Layer.Start);
			const float End = Layer.Attack + Layer.Hold + Layer.Decay;
			float Envelope = 0.f;
			if (T < Layer.Attack)
			{
				Envelope = T / Layer.Attack;
			}
			else if (T < Layer.Attack + Layer.Hold)
			{
				Envelope = 1.f;
			}
			else if (T < End)
			{
				Envelope = 1.f - (T - Layer.Attack - Layer.Hold) / Layer.Decay;
			}
			// 比一帧还短的层：第一次轮到它时至少按满振幅出一帧，不然 90Hz 下 6ms 的"嗒"会整个漏掉
			if (!Layer.bEmitted)
			{
				Layer.bEmitted = true;
				if (Layer.Attack < 0.02f)
				{
					Envelope = 1.f;
				}
			}
			Sum += Layer.Amplitude * Envelope;
			if (T >= End)
			{
				Stack.RemoveAt(Index);
			}
		}
		const float Output = FMath::Clamp(Sum, 0.f, 1.f) * ToySettings::HapticGain();
		if (bVR && PC && (Output > 0.005f || HapticOutput[Side] > 0.005f))
		{
			PC->SetHapticsByValue(1.f, Output, Side == 0 ? EControllerHand::Left : EControllerHand::Right);
		}
		HapticOutput[Side] = Output;
	}

	// toy.HapticTest：信息牌跟着步骤换字，全部放完收起
	if (HapticTestSteps.Num() > 0 && RightHand)
	{
		const float Elapsed = static_cast<float>(Now - HapticTestStart);
		int32 Step = INDEX_NONE;
		for (int32 Index = 0; Index < HapticTestSteps.Num(); ++Index)
		{
			if (Elapsed >= HapticTestSteps[Index].Key)
			{
				Step = Index;
			}
		}
		const bool bDone = Elapsed > HapticTestSteps.Last().Key + 1.5f;
		if (bDone)
		{
			HapticTestSteps.Reset();
			HapticTestShown = INDEX_NONE;
			RightHand->HideCustomInfo();
			UE_LOG(LogTemp, Log, TEXT("ToyBox: 震动测试放完"));
		}
		else if (Step != HapticTestShown && Step != INDEX_NONE)
		{
			HapticTestShown = Step;
			RightHand->ShowCustomInfo(FText::FromString(TEXT("震动测试")), FText::FromString(HapticTestSteps[Step].Value));
			UE_LOG(LogTemp, Log, TEXT("ToyBox: 震动测试 %s"), *HapticTestSteps[Step].Value);
		}
	}
}

void AToyBoxPawn::StartHapticTest(int32 Mode)
{
	HapticTestSteps.Reset();
	HapticTestShown = INDEX_NONE;
	HapticTestStart = GetWorld()->GetRealTimeSeconds();
	auto Pulse = [this](float At, float Amplitude, float Attack, float Hold, float Decay)
	{
		FToyHapticLayer Layer;
		Layer.Delay = At;
		Layer.Amplitude = Amplitude;
		Layer.Attack = Attack;
		Layer.Hold = Hold;
		Layer.Decay = Decay;
		AddHaptic(EControllerHand::Right, Layer);
	};
	auto Step = [this](float At, const FString& Label) { HapticTestSteps.Emplace(At, Label); };

	switch (Mode)
	{
	case 1:
	{
		// 振幅阶梯：同样 30ms，越来越强；找"刚刚感觉得到"的那一档
		const float Amps[] = { 0.05f, 0.1f, 0.15f, 0.2f, 0.3f, 0.4f, 0.5f };
		for (int32 K = 0; K < UE_ARRAY_COUNT(Amps); ++K)
		{
			const float At = 0.5f + K * 1.2f;
			Step(At - 0.3f, FString::Printf(TEXT("1 振幅 %d/%d：%.2f，30ms"), K + 1, UE_ARRAY_COUNT(Amps), Amps[K]));
			Pulse(At, Amps[K], 0.f, 0.03f, 0.f);
		}
		break;
	}
	case 2:
	{
		// 时长阶梯：同样 0.6，越来越长；找"像一下点击"和"像一下嗡"的分界
		const float Durations[] = { 0.005f, 0.01f, 0.02f, 0.03f, 0.05f };
		for (int32 K = 0; K < UE_ARRAY_COUNT(Durations); ++K)
		{
			const float At = 0.5f + K * 1.2f;
			Step(At - 0.3f, FString::Printf(TEXT("2 时长 %d/%d：%.0fms，0.6"), K + 1, UE_ARRAY_COUNT(Durations), Durations[K] * 1000.f));
			Pulse(At, 0.6f, 0.f, Durations[K], 0.f);
		}
		break;
	}
	case 3:
		// 干脆的"咔"和渐强渐弱的"嗡"交替三遍
		for (int32 K = 0; K < 3; ++K)
		{
			const float At = 0.5f + K * 2.4f;
			Step(At - 0.3f, FString::Printf(TEXT("3 点击 %d/3：0.8，8ms"), K + 1));
			Pulse(At, 0.8f, 0.f, 0.008f, 0.f);
			Step(At + 0.9f, FString::Printf(TEXT("3 包络 %d/3：0.6，升 40ms + 停 40ms + 降 120ms"), K + 1));
			Pulse(At + 1.2f, 0.6f, 0.04f, 0.04f, 0.12f);
		}
		break;
	case 4:
	{
		// 伺服"齿轮感"：0.12 的 6ms 小脉冲，间隔 30 / 60 / 100ms 各放 1 秒
		const float Gaps[] = { 0.03f, 0.06f, 0.1f };
		for (int32 K = 0; K < UE_ARRAY_COUNT(Gaps); ++K)
		{
			const float At = 0.5f + K * 2.f;
			Step(At - 0.3f, FString::Printf(TEXT("4 齿轮 %d/3：0.12，6ms，每 %.0fms 一下"), K + 1, Gaps[K] * 1000.f));
			for (float T = 0.f; T < 1.f; T += Gaps[K])
			{
				Pulse(At + T, 0.12f, 0.f, 0.006f, 0.f);
			}
		}
		break;
	}
	default:
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.HapticTest 1 = 振幅阶梯，2 = 时长阶梯，3 = 点击 vs 包络，4 = 齿轮"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 震动测试 %d 开始，共 %d 步%s"), Mode, HapticTestSteps.Num(), bVR ? TEXT("") : TEXT("（桌面模式没有震动，只显示步骤）"));
}
