#include "ToyBoxPawn.h"

#include "ToyCube.h"
#include "ToyHand.h"
#include "ToyLilliput.h"
#include "ToyMenuPanel.h"
#include "ToySettings.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MotionControllerComponent.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	FAutoConsoleCommandWithWorldAndArgs FigurineCommand(TEXT("toy.Figurine"),
		TEXT("小人国：把小人偶放到工作室坐标 (X, Y) 最高的台面上（茶几、餐桌、柜子顶），朝着 Yaw（度，默认面朝 -X）：toy.Figurine <X> <Y> [Yaw]。")
		TEXT("截图测试用，比如 toy.Figurine -440 -40 再 toy.Shrink = 站在茶几上"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AToyFigurine* Figurine = AToyFigurine::Find(World);
			AToyStudio* Studio = AToyStudio::Find(World);
			if (!Figurine || !Studio || Args.Num() < 2 || Figurine->IsHidden())
			{
				return;
			}
			const FTransform StudioTransform = Studio->GetActorTransform();
			const FVector Top = StudioTransform.TransformPosition(FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 250.f));
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyFigurinePlace), false, Figurine);
			Params.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(World, 0));
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
			{
				const float Yaw = StudioTransform.Rotator().Yaw + (Args.Num() > 2 ? FCString::Atof(*Args[2]) : 180.f);
				Figurine->StandAt(Hit.ImpactPoint + FVector(0.f, 0.f, 0.7f), Yaw);
				UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：小人偶放到 %s（%s 上）"), *Hit.ImpactPoint.ToCompactString(), *GetNameSafe(Hit.GetActor()));
			}
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

	FAutoConsoleCommandWithWorldAndArgs ReachMapCommand(TEXT("toy.ReachMap"),
		TEXT("走得到 / 传送得到的地图：toy.ReachMap [格子厘米，默认 10] [文件名，默认 reach] [胶囊体半径，不写 = 不变]，写 Saved/ReachMap/<文件名>.csv（Tools/reach_map.py 画成图）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr)
			{
				if (Args.Num() > 2)
				{
					Pawn->CapsuleRadius = FCString::Atof(*Args[2]);
				}
				Pawn->RunReachMap(Args.Num() > 0 ? FMath::Max(2.f, FCString::Atof(*Args[0])) : 10.f, Args.Num() > 1 ? Args[1] : TEXT("reach"));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs AimPreviewCommand(TEXT("toy.AimPreview"),
		TEXT("截图用（桌面模式）：右手摆到镜头右下方、朝 (Yaw, Pitch)（相对镜头，度）一直瞄准传送，不会真的传送：toy.AimPreview <Yaw> <Pitch>；toy.AimPreview off 关掉"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr)
			{
				const bool bOn = Args.Num() >= 2;
				Pawn->SetAimPreview(bOn, bOn ? FRotator(FCString::Atof(*Args[1]), FCString::Atof(*Args[0]), 0.f) : FRotator::ZeroRotator);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ShrinkCommand(TEXT("toy.Shrink"),
		TEXT("小人国：在大身体和小人偶之间切换一次（和左手柄 Y、桌面模式 G 一样）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AToyBoxPawn* Pawn = World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr)
			{
				Pawn->ToggleBody();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs HideHandsCommand(TEXT("toy.HideHands"),
		TEXT("截图用：toy.HideHands 1 把两只手藏起来（桌面模式右手跟着鼠标，会挡在画面中间），0 = 显示"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bHide = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
			for (TActorIterator<AToyHand> It(World); It; ++It)
			{
				It->SetActorHiddenInGame(bHide);
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

	/** 传送抛物线和落点圈的材质：不受光照、半透明，颜色 = Color 参数 × 顶点颜色，不透明度 = Opacity × 顶点 alpha（没有就退回虚影材质）。 */
	const TCHAR* ArcMaterialPath = TEXT("/Game/Materials/M_ToyArc.M_ToyArc");
	const TCHAR* GlowMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float Glow = 12.f;
	// 抛物线管子：沿路径等距 ArcRings 节，每节一圈 ArcSides 个顶点。芯的半径至少 ArcCoreRadius，离眼睛远了按距离加粗（保持 2~3 个像素宽，
	// 太细了 MSAA 下会一闪一闪）；外面一层光晕粗 ArcHaloScale 倍、很淡
	constexpr int32 ArcRings = 48;
	constexpr int32 ArcSides = 8;
	constexpr float ArcCoreRadius = 0.45f;
	constexpr float ArcRadiusPerCm = 0.0016f;
	constexpr float ArcHaloScale = 3.5f;
	// 落点圈（厘米）：亮环中线半径、亮环宽度、里面那层淡底的不透明度
	constexpr float RingRadius = 21.f;
	constexpr float RingWidth = 6.f;
	constexpr float RingFill = 0.16f;
	constexpr int32 RingSegments = 64;

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
	// 小人国：左手 Y = 变小 / 变大，左手 X = 叫出 / 收起主菜单（没有桌上的触屏了，菜单靠它叫出来）
	SizeSwitchKeys = Keys({ TEXT("OculusTouch_Left_Y_Click"), TEXT("ValveIndex_Left_B_Click") });
	MenuKeys = Keys({ TEXT("OculusTouch_Left_X_Click"), TEXT("ValveIndex_Left_A_Click") });

	// 传送瞄准的显示：一根细管当抛物线，一个亮环当落点圈（网格在 BuildTeleportMeshes 里建）。都只显示，不碰撞、不投影
	TeleportArc = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TeleportArc"));
	TeleportArc->SetupAttachment(VROrigin);
	TeleportRing = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TeleportRing"));
	TeleportRing->SetupAttachment(VROrigin);
	TeleportRing->SetUsingAbsoluteLocation(true);
	TeleportRing->SetUsingAbsoluteRotation(true);
	TeleportRing->SetUsingAbsoluteScale(true);
	for (UProceduralMeshComponent* Mesh : { TeleportArc.Get(), TeleportRing.Get() })
	{
		Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Mesh->SetCastShadow(false);
		Mesh->bUseAsyncCooking = false;
		Mesh->SetHiddenInGame(true);
	}
}

void AToyBoxPawn::BeginPlay()
{
	Super::BeginPlay();
	ToyHandCalib::Load();
	ToySettings::Load();
	// 上一次 PIE 可能是在小身体时结束的：比例是全局的，开局先恢复成大身体
	ToyLilliput::SetScale(GetWorld(), 1.f);
	LeftHand = SpawnHand(EControllerHand::Left, LeftController);
	RightHand = SpawnHand(EControllerHand::Right, RightController);
	SpawnLilliput();
	LogHandSelfCheck();
	BuildTeleportMeshes();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ArcMaterialPath);
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 没有 %s（跑一遍 Tools/create_toy_materials.py），传送抛物线先用虚影材质"), ArcMaterialPath);
		Base = LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath);
	}
	if (Base)
	{
		TeleportMaterial = UMaterialInstanceDynamic::Create(Base, this);
		TeleportMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		for (int32 Section = 0; Section < 2; ++Section)
		{
			TeleportArc->SetMaterial(Section, TeleportMaterial);
			TeleportRing->SetMaterial(Section, TeleportMaterial);
		}
	}
}

void AToyBoxPawn::BuildTeleportMeshes()
{
	// 抛物线：芯（段 0）和光晕（段 1）是同样拓扑的两根管子。三角形只建这一次，之后 UpdateMeshSection 只换顶点位置、颜色
	const int32 NumVerts = ArcRings * ArcSides;
	TArray<int32> Tris;
	Tris.Reserve((ArcRings - 1) * ArcSides * 6);
	for (int32 Ring = 0; Ring < ArcRings - 1; ++Ring)
	{
		for (int32 Side = 0; Side < ArcSides; ++Side)
		{
			const int32 A = Ring * ArcSides + Side;
			const int32 B = Ring * ArcSides + (Side + 1) % ArcSides;
			Tris.Append({ A, A + ArcSides, B, B, A + ArcSides, B + ArcSides });
		}
	}
	ArcPath.SetNumZeroed(ArcRings);
	ArcCoreVerts.SetNumZeroed(NumVerts);
	ArcHaloVerts.SetNumZeroed(NumVerts);
	ArcCoreColors.Init(FLinearColor::Transparent, NumVerts);
	ArcHaloColors.Init(FLinearColor::Transparent, NumVerts);
	const TArray<FVector> NoNormals;
	const TArray<FVector2D> NoUV;
	const TArray<FProcMeshTangent> NoTangents;
	TeleportArc->CreateMeshSection_LinearColor(0, ArcCoreVerts, Tris, NoNormals, NoUV, ArcCoreColors, NoTangents, false);
	TeleportArc->CreateMeshSection_LinearColor(1, ArcHaloVerts, Tris, NoNormals, NoUV, ArcHaloColors, NoTangents, false);

	// 落点圈（段 0）：三圈顶点，中间那圈全亮、内外两圈透明，亮环两边是虚的；段 1：里面一层很淡的底，中心稍亮
	TArray<FVector> Verts;
	TArray<FLinearColor> Colors;
	Tris.Reset();
	const float Radii[3] = { RingRadius - RingWidth * 0.5f, RingRadius, RingRadius + RingWidth * 0.5f };
	for (int32 Seg = 0; Seg < RingSegments; ++Seg)
	{
		const float Angle = 2.f * PI * Seg / RingSegments;
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		for (int32 Loop = 0; Loop < 3; ++Loop)
		{
			Verts.Add(Dir * Radii[Loop]);
			Colors.Add(FLinearColor(1.f, 1.f, 1.f, Loop == 1 ? 1.f : 0.f));
		}
		const int32 Next = (Seg + 1) % RingSegments;
		for (int32 Loop = 0; Loop < 2; ++Loop)
		{
			const int32 A = Seg * 3 + Loop;
			const int32 B = Next * 3 + Loop;
			Tris.Append({ A, B, A + 1, A + 1, B, B + 1 });
		}
	}
	TeleportRing->CreateMeshSection_LinearColor(0, Verts, Tris, NoNormals, NoUV, Colors, NoTangents, false);
	Verts.Reset();
	Colors.Reset();
	Tris.Reset();
	Verts.Add(FVector::ZeroVector);
	Colors.Add(FLinearColor(1.f, 1.f, 1.f, RingFill * 1.6f));
	for (int32 Seg = 0; Seg < RingSegments; ++Seg)
	{
		const float Angle = 2.f * PI * Seg / RingSegments;
		Verts.Add(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radii[0]);
		Colors.Add(FLinearColor(1.f, 1.f, 1.f, RingFill));
		Tris.Append({ 0, 1 + (Seg + 1) % RingSegments, 1 + Seg });
	}
	TeleportRing->CreateMeshSection_LinearColor(1, Verts, Tris, NoNormals, NoUV, Colors, NoTangents, false);
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
	// 编辑器里停止 PIE 后视口也用 WorldToMeters 和近裁剪面，恢复成正常比例
	ToyLilliput::SetScale(GetWorld(), 1.f);
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
	if (bTiny)
	{
		TickTinyGround(DeltaSeconds);
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
		Camera->SetRelativeLocationAndRotation(FVector(0.f, 0.f, DesktopEyeHeight * ToyLilliput::GetScale()), FRotator(-40.f, 0.f, 0.f));
		LeftHand->ReleaseHeld(false);
		LeftHand->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		LeftHand->SetActorHiddenInGame(true);
		LeftHand->SetActorTickEnabled(false);
		RightHand->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		// 桌面模式下手跟着鼠标走，靠鼠标点击来按按钮，避免手划过屏幕时误触
		RightHand->SetPokeEnabled(false);
		RightHand->SetActorScale3D(FVector(ToyLilliput::GetScale()));
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
	if (!bCalibApplied || !(ToyHandCalib::Get() == AppliedCalib) || AppliedHandScale != ToyLilliput::GetScale())
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

	// 小人国：左手 Y 换身体，左手 X 叫出 / 收起菜单（按下那一下算一次）
	const bool bSizeDown = ReadAxis(SizeSwitchKeys) > 0.5f;
	if (bSizeDown && !bSizeSwitchWasDown && ToyLilliput::IsEnabled())
	{
		ToggleBody();
	}
	bSizeSwitchWasDown = bSizeDown;
	const bool bMenuDown = ReadAxis(MenuKeys) > 0.5f;
	if (bMenuDown && !bMenuWasDown)
	{
		for (TActorIterator<AToyMenuPanel> It(GetWorld()); It; ++It)
		{
			It->Toggle();
			break;
		}
	}
	bMenuWasDown = bMenuDown;

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
		bTeleportValid = UpdateTeleportAim(Hand.GetLocation(), Hand.GetUnitAxis(EAxis::X), RightAim->IsTracked() ? RightAim : RightController);
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
	AppliedHandScale = ToyLilliput::GetScale();
	bCalibApplied = true;
	if (!bVR)
	{
		return;   // 桌面模式手不挂在控制器上，姿态由 TickDesktop 摆，不受校准影响
	}
	// 小身体：手跟着缩小，校准的位置偏移（厘米）也一起缩
	for (AToyHand* Hand : { LeftHand.Get(), RightHand.Get() })
	{
		FTransform Relative = ToyHandCalib::HandTransform(Hand == LeftHand.Get() ? EControllerHand::Left : EControllerHand::Right, AppliedCalib);
		Relative.ScaleTranslation(AppliedHandScale);
		Relative.SetScale3D(Relative.GetScale3D() * AppliedHandScale);
		Hand->SetActorRelativeTransform(Relative);
	}
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
		* ToySettings::MoveSpeedCm() * ToyLilliput::GetScale() * DeltaSeconds;

	MoveWithCollision(Delta);
}

bool AToyBoxPawn::MoveWithCollision(FVector Move, FHitResult* OutHit)
{
	// 胶囊体立在头的正下方，从现在的位置扫到目标位置；碰到墙 / 桌子就停在碰撞点前，再沿墙面滑一下
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyMove), false, this);
	AddIgnoredForMove(Params);
	// 小身体：胶囊体、离地高度、贴墙距离都按比例缩
	const float Scale = ToyLilliput::GetScale();
	const float Lift = CapsuleLift * Scale;
	const float HalfHeight = CapsuleHalfHeight * Scale;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius * Scale, HalfHeight);
	// 碰到东西时停在碰撞点前这么远，不贴着。贴着停的话下一帧扫掠一开始就"嵌在里面"，以前那种情况只许往外退、不许沿墙滑，
	// 走到门框边上就再也挪不动了（门口卡住就是这个）
	const float Skin = 0.5f * Scale;
	const FVector Requested = Move;
	const FVector Start = GetActorLocation();
	bool bBlocked = false;
	for (int32 Pass = 0; Pass < 3 && !Move.IsNearlyZero(); ++Pass)
	{
		const FVector Center = GetFeetLocation() + FVector(0.f, 0.f, Lift + HalfHeight);
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
				AddActorWorldOffset(Normal * (FMath::Min(Hit.PenetrationDepth, 5.f * Scale) + Skin));
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
		const FVector Center = GetFeetLocation() + FVector(0.f, 0.f, Lift + HalfHeight);
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

void AToyBoxPawn::RunReachMap(float Cell, const FString& Name)
{
	const AToyStudio* Studio = AToyStudio::Find(GetWorld());
	if (!Studio)
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	const FTransform StudioTransform = Studio->GetActorTransform();
	const float FloorZ = StudioTransform.GetLocation().Z;
	const float FeetZ = GetActorLocation().Z;
	FBox2D Bounds(FVector2D(Studio->WalkArea.Min), FVector2D(Studio->WalkArea.Max));
	for (const FBox& Area : Studio->ExtraWalkAreas)
	{
		Bounds += FVector2D(Area.Min);
		Bounds += FVector2D(Area.Max);
	}
	Bounds = Bounds.ExpandBy(150.f);
	const int32 NX = FMath::CeilToInt((Bounds.Max.X - Bounds.Min.X) / Cell) + 1;
	const int32 NY = FMath::CeilToInt((Bounds.Max.Y - Bounds.Min.Y) / Cell) + 1;
	auto Local = [&](int32 IX, int32 IY) { return FVector2D(Bounds.Min.X + IX * Cell, Bounds.Min.Y + IY * Cell); };
	auto World3 = [&](FVector2D P, float Z) { FVector V = StudioTransform.TransformPosition(FVector(P.X, P.Y, 0.f)); V.Z = Z; return V; };
	auto Describe = [](const UPrimitiveComponent* C, const AActor* A)
	{
		const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
		FString Text = FString::Printf(TEXT("%s/%s"), A ? *A->GetActorNameOrLabel() : TEXT("?"),
			SM && SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : (C ? *C->GetName() : TEXT("?")));
		return Text.Replace(TEXT(","), TEXT(";"));
	};

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyReachMap), false, this);
	AddIgnoredForMove(Params);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	const float CapsuleZ = FeetZ + CapsuleLift + CapsuleHalfHeight;

	// 每一格：地板（1 = 从上往下第一下就打到站立高度的朝上表面，2 = 上面压着家具、底下才是地），能不能传送，胶囊体站不站得下
	TArray<uint8> Floor, Tele, Free, Walk;
	TArray<FString> TopText, BlockText;
	const int32 N = NX * NY;
	Floor.SetNumZeroed(N);
	Tele.SetNumZeroed(N);
	Free.SetNumZeroed(N);
	Walk.SetNumZeroed(N);
	TopText.SetNum(N);
	BlockText.SetNum(N);
	for (int32 IY = 0; IY < NY; ++IY)
	{
		for (int32 IX = 0; IX < NX; ++IX)
		{
			const int32 I = IY * NX + IX;
			const FVector2D P = Local(IX, IY);
			// 从 2m 高往下打：吊扇、吊饰在 2.2~2.4m，从更高处打会一开始就嵌在它们里面
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, World3(P, FloorZ + 200.f), World3(P, FloorZ - 50.f), ECC_Visibility, Params))
			{
				TopText[I] = FString::Printf(TEXT("%s@%.0f"), *Describe(Hit.GetComponent(), Hit.GetActor()), Hit.ImpactPoint.Z - FloorZ);
				if (Hit.ImpactNormal.Z > 0.8f && FMath::Abs(Hit.ImpactPoint.Z - FloorZ) < 5.f)
				{
					Floor[I] = 1;
				}
				Tele[I] = IsTeleportTargetValid(Hit) ? 1 : 0;
			}
			FHitResult Low;
			if (!Floor[I] && GetWorld()->LineTraceSingleByChannel(Low, World3(P, FloorZ + 12.f), World3(P, FloorZ - 20.f), ECC_Visibility, Params)
				&& Low.ImpactNormal.Z > 0.8f)
			{
				Floor[I] = 2;
			}
			TArray<FOverlapResult> Overlaps;
			if (GetWorld()->OverlapMultiByChannel(Overlaps, World3(P, CapsuleZ), FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				for (const FOverlapResult& O : Overlaps)
				{
					if (O.bBlockingHit)
					{
						BlockText[I] = Describe(O.GetComponent(), O.GetActor());
						break;
					}
				}
			}
			Free[I] = BlockText[I].IsEmpty() ? 1 : 0;
		}
	}

	// 从开局站立点（工作室原点）往四周一格一格扫掠，和摇杆走路用同一个胶囊体
	const int32 SX = FMath::RoundToInt((0.f - Bounds.Min.X) / Cell);
	const int32 SY = FMath::RoundToInt((0.f - Bounds.Min.Y) / Cell);
	TArray<int32> Queue;
	if (SX >= 0 && SX < NX && SY >= 0 && SY < NY && Free[SY * NX + SX])
	{
		Walk[SY * NX + SX] = 1;
		Queue.Add(SY * NX + SX);
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 IX = Queue[Head] % NX;
		const int32 IY = Queue[Head] / NX;
		const FVector From = World3(Local(IX, IY), CapsuleZ);
		for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			const int32 JX = IX + D.X;
			const int32 JY = IY + D.Y;
			if (JX < 0 || JX >= NX || JY < 0 || JY >= NY)
			{
				continue;
			}
			const int32 J = JY * NX + JX;
			if (Walk[J] || !Free[J])
			{
				continue;
			}
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByChannel(Hit, From, World3(Local(JX, JY), CapsuleZ), FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				Walk[J] = 1;
				Queue.Add(J);
			}
		}
	}

	TArray<FString> Rows;
	Rows.Add(FString::Printf(TEXT("# cell %.1f radius %.1f halfheight %.1f lift %.1f floorz %.1f feetz %.1f"),
		Cell, CapsuleRadius, CapsuleHalfHeight, CapsuleLift, FloorZ, FeetZ));
	Rows.Add(FString::Printf(TEXT("# area walk %.0f %.0f %.0f %.0f"), Studio->WalkArea.Min.X, Studio->WalkArea.Min.Y, Studio->WalkArea.Max.X, Studio->WalkArea.Max.Y));
	for (const FBox& Area : Studio->ExtraWalkAreas)
	{
		Rows.Add(FString::Printf(TEXT("# area extra %.0f %.0f %.0f %.0f"), Area.Min.X, Area.Min.Y, Area.Max.X, Area.Max.Y));
	}
	Rows.Add(TEXT("ix,iy,x,y,floor,tele,free,walk,top,blocker"));
	int32 Counts[4] = { 0, 0, 0, 0 };
	for (int32 I = 0; I < N; ++I)
	{
		const FVector2D P = Local(I % NX, I / NX);
		Rows.Add(FString::Printf(TEXT("%d,%d,%.0f,%.0f,%d,%d,%d,%d,%s,%s"), I % NX, I / NX, P.X, P.Y,
			Floor[I], Tele[I], Free[I], Walk[I], *TopText[I], *BlockText[I]));
		if (Floor[I] == 1)
		{
			++Counts[(Walk[I] ? 1 : 0) + (Tele[I] ? 2 : 0)];
		}
	}
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ReachMap") / (Name + TEXT(".csv")));
	FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")) + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	UE_LOG(LogTemp, Display, TEXT("ReachMap: %d×%d 格（%.0fcm），胶囊体半径 %.0f 半高 %.0f 离地 %.0f；露出来的地板里 走+传 %d、只能传 %d、只能走 %d、都不行 %d；用时 %.1fs → %s"),
		NX, NY, Cell, CapsuleRadius, CapsuleHalfHeight, CapsuleLift, Counts[3], Counts[2], Counts[1], Counts[0],
		FPlatformTime::Seconds() - StartTime, *Path);
}

bool AToyBoxPawn::IsTeleportTargetValid(const FHitResult& Hit) const
{
	// 只能落在工作室的地板上（地毯也算地板；套用样板间时是样板间的地面），房间以内，脚下那块地方站得下一个胶囊体（不在桌子底下、不贴墙）
	// 小身体：桌面、书顶、沙发垫，任何朝上、站得下的表面都能去（不限地板）
	const AToyStudio* Studio = AToyStudio::Find(GetWorld());
	if (bTiny ? Hit.ImpactNormal.Z < 0.7f : (!Studio || !Studio->IsFloorHit(Hit)))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTeleport), false, this);
	AddIgnoredForMove(Params);
	const float Scale = ToyLilliput::GetScale();
	const FVector Center = Hit.ImpactPoint + FVector(0.f, 0.f, (CapsuleLift + CapsuleHalfHeight) * Scale);
	return !GetWorld()->OverlapAnyTestByChannel(Center, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius * Scale, CapsuleHalfHeight * Scale), Params);
}

bool AToyBoxPawn::UpdateTeleportAim(const FVector& Start, const FVector& Direction, USceneComponent* Source)
{
	// 小身体：出射速度和重力都乘比例，抛物线形状不变、整体缩小
	const float Scale = ToyLilliput::GetScale();
	FPredictProjectilePathParams Path(1.f * Scale, Start, Direction.GetSafeNormal() * TeleportLaunchSpeed * Scale, 2.f, ECC_Visibility, this);
	Path.OverrideGravityZ = GetWorld()->GetGravityZ() * Scale;
	Path.bTraceWithCollision = true;
	Path.bTraceComplex = false;
	Path.SimFrequency = 20.f;
	Path.ActorsToIgnore = { LeftHand, RightHand };
	if (LeftHand && LeftHand->GetHeldActor()) Path.ActorsToIgnore.Add(LeftHand->GetHeldActor());
	if (RightHand && RightHand->GetHeldActor()) Path.ActorsToIgnore.Add(RightHand->GetHeldActor());
	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(GetWorld(), Path, Result);
	const bool bValid = bHit && IsTeleportTargetValid(Result.HitResult);

	// 抛物线：管子挂在发出抛物线的组件下面（VR 里是右手柄），渲染前手柄的低延迟更新会带着它一起动，起点贴着手柄不抖
	if (Source && TeleportArc->GetAttachParent() != Source)
	{
		TeleportArc->AttachToComponent(Source, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	UpdateArcMesh(Result.PathData, TeleportArc->GetAttachParent());
	if (TeleportMaterial && TeleportColorState != (bValid ? 1 : 0))
	{
		TeleportColorState = bValid ? 1 : 0;
		const FLinearColor Color = bValid ? FLinearColor(0.2f, 0.85f, 1.f) : FLinearColor(1.f, 0.18f, 0.12f);
		// 和触屏、信息牌一样跟着场景的曝光提亮（样板间是固定曝光再减 1.5 档，不提亮的话抛物线是暗青色）
		const AToyStudio* Studio = AToyStudio::Find(GetWorld());
		TeleportMaterial->SetVectorParameterValue(TEXT("Color"), Color * (Glow * (Studio ? Studio->UiBrightnessScale : 1.f)));
	}
	TeleportArc->SetHiddenInGame(Result.PathData.Num() < 2);
	// 落点圈轻轻地"呼吸"（±4%）
	const float Breath = 1.f + 0.04f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 5.f);
	TeleportRing->SetWorldScale3D(FVector(Breath * Scale, Breath * Scale, Scale));
	if (bValid)
	{
		TeleportRing->SetHiddenInGame(false);
		TeleportRing->SetWorldLocation(Result.HitResult.ImpactPoint + FVector(0.f, 0.f, 0.6f * Scale));
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
		// 套用样板间时，样板间的地面也算地（只认站立高度的，不然红圈会停在衣帽架的挂钩、柜子顶上）
		const AToyStudio* Studio = AToyStudio::Find(GetWorld());
		if (GetWorld()->LineTraceSingleByChannel(Floor, From, From - FVector(0.f, 0.f, 500.f), ECC_Visibility, FloorParams)
			&& Floor.ImpactNormal.Z > 0.8f && (Cast<AToyStudio>(Floor.GetActor())
				|| (Studio && !Studio->bBuildRoom && FMath::Abs(Floor.ImpactPoint.Z - Studio->GetActorLocation().Z) < 5.f)))
		{
			TeleportRing->SetWorldLocation(Floor.ImpactPoint + FVector(0.f, 0.f, 0.6f * Scale));
			bRingOnFloor = true;
		}
	}
	TeleportRing->SetHiddenInGame(!bRingOnFloor);
	return false;
}

void AToyBoxPawn::UpdateArcMesh(const TArray<FPredictProjectilePathPointData>& Points, const USceneComponent* Space)
{
	if (Points.Num() < 2 || !Space)
	{
		return;
	}
	// 按弧长等距重新采样成 ArcRings 个点（路径点是按模拟时间等间隔的，越往下越稀）
	ArcPathLength.SetNum(Points.Num(), EAllowShrinking::No);
	ArcPathLength[0] = 0.f;
	for (int32 I = 1; I < Points.Num(); ++I)
	{
		ArcPathLength[I] = ArcPathLength[I - 1] + FVector::Dist(Points[I - 1].Location, Points[I].Location);
	}
	const float Total = FMath::Max(ArcPathLength.Last(), 1.f);
	int32 Seg = 0;
	for (int32 Ring = 0; Ring < ArcRings; ++Ring)
	{
		const float L = Total * Ring / (ArcRings - 1);
		while (Seg < Points.Num() - 2 && ArcPathLength[Seg + 1] < L)
		{
			++Seg;
		}
		const float SegLength = FMath::Max(ArcPathLength[Seg + 1] - ArcPathLength[Seg], KINDA_SMALL_NUMBER);
		ArcPath[Ring] = FMath::Lerp(Points[Seg].Location, Points[Seg + 1].Location, FMath::Clamp((L - ArcPathLength[Seg]) / SegLength, 0.f, 1.f));
	}

	// 每节一圈顶点：截面方向沿路径平行移动（不会在抛物线顶点附近突然翻转），顶点算到管子挂着的组件的局部坐标
	const FTransform ToLocal = Space->GetComponentTransform();
	const FVector Eye = Camera->GetComponentLocation();
	const float Time = GetWorld()->GetRealTimeSeconds();
	FVector Normal = FVector::ZeroVector;
	for (int32 Ring = 0; Ring < ArcRings; ++Ring)
	{
		const FVector Tangent = (ArcPath[FMath::Min(Ring + 1, ArcRings - 1)] - ArcPath[FMath::Max(Ring - 1, 0)]).GetSafeNormal();
		if (Ring == 0)
		{
			Normal = FVector::CrossProduct(Tangent, FVector::UpVector);
			if (Normal.SizeSquared() < 0.01f)
			{
				Normal = FVector::CrossProduct(Tangent, FVector::ForwardVector);
			}
		}
		Normal = (Normal - Tangent * FVector::DotProduct(Normal, Tangent)).GetSafeNormal();
		const FVector Binormal = FVector::CrossProduct(Tangent, Normal);

		// S = 0（手）→ 1（落点）。手边细、淡，往前慢慢变亮，到落点最亮；一串很淡的亮纹顺着往落点流
		const float S = static_cast<float>(Ring) / (ArcRings - 1);
		const float FadeIn = FMath::SmoothStep(0.f, 0.14f, S);
		const float Radius = FMath::Max(ArcCoreRadius * ToyLilliput::GetScale(), ArcRadiusPerCm * FVector::Dist(Eye, ArcPath[Ring])) * FMath::Lerp(0.4f, 1.f, FadeIn);
		const float Flow = 0.88f + 0.12f * FMath::Sin(2.f * PI * (S * 7.f - Time * 1.6f));
		const float Bright = FMath::Lerp(0.45f, 1.f, S * S) * Flow;
		const FLinearColor Core(Bright, Bright, Bright, 0.9f * FadeIn);
		const FLinearColor Halo(Bright, Bright, Bright, 0.13f * FadeIn);
		for (int32 Side = 0; Side < ArcSides; ++Side)
		{
			const float Angle = 2.f * PI * Side / ArcSides;
			const FVector Offset = Normal * FMath::Cos(Angle) + Binormal * FMath::Sin(Angle);
			const int32 V = Ring * ArcSides + Side;
			ArcCoreVerts[V] = ToLocal.InverseTransformPosition(ArcPath[Ring] + Offset * Radius);
			ArcHaloVerts[V] = ToLocal.InverseTransformPosition(ArcPath[Ring] + Offset * (Radius * ArcHaloScale));
			ArcCoreColors[V] = Core;
			ArcHaloColors[V] = Halo;
		}
	}
	const TArray<FVector> NoNormals;
	const TArray<FVector2D> NoUV;
	const TArray<FProcMeshTangent> NoTangents;
	TeleportArc->UpdateMeshSection_LinearColor(0, ArcCoreVerts, NoNormals, NoUV, ArcCoreColors, NoTangents, false);
	TeleportArc->UpdateMeshSection_LinearColor(1, ArcHaloVerts, NoNormals, NoUV, ArcHaloColors, NoTangents, false);
}

void AToyBoxPawn::HideTeleportAim()
{
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
		FallVelocity = 0.f;
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
	DesktopHoldDistance = FMath::Clamp(DesktopHoldDistance, 25.f * ToyLilliput::GetScale(), 200.f);

	// toy.AimPreview：右手像拿着手柄一样摆在镜头右下方，从手里一直打传送抛物线（截图看效果用，不传送）。
	// 放在读鼠标前面：-game 截图时窗口没有焦点、拿不到鼠标位置
	if (bAimPreview)
	{
		const FRotator CameraRotation = Camera->GetComponentRotation();
		const FVector Aim = (CameraRotation + AimPreviewAngles).Vector();
		const FRotationMatrix View(CameraRotation);
		const FVector HandLocation = Camera->GetComponentLocation() + View.GetUnitAxis(EAxis::X) * 55.f
			+ View.GetUnitAxis(EAxis::Y) * 24.f - View.GetUnitAxis(EAxis::Z) * 28.f;
		RightHand->SetDesktopPose(HandLocation, FRotator(Aim.Rotation().Pitch, Aim.Rotation().Yaw, -90.f));
		RightHand->SetInputs(0.f, 0.f);
		bTeleportAiming = true;
		bTeleportValid = UpdateTeleportAim(HandLocation + Aim * 10.f, Aim, VROrigin);
		return;
	}

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
		DesktopHoldDistance = FMath::Clamp(Hit.Distance, 25.f * ToyLilliput::GetScale(), 200.f);
	}
	// 鼠标点中的零件优先：它的检测盒可能和旁边零件的盒子重叠
	RightHand->SetPreferredTarget(bHit ? Hit.GetActor() : nullptr);
	// 手指沿视线方向、掌心朝下，像从上面抓东西
	// 只取视线的水平朝向（不跟着低头），Roll -90 让掌心真的朝下（+90 是掌心朝上，抓零件时零件会翻到手掌上面去）
	RightHand->SetDesktopPose(GripTarget, FRotator(0.f, Direction.Rotation().Yaw, -90.f));
	RightHand->SetInputs(0.f, bLeftDown && !bDesktopClickIsPoke ? 1.f : 0.f);

	// M = 叫出 / 收起主菜单（VR 里戳触屏右上角的 MENU）
	// G = 小人国换身体（VR 里左手柄 Y）
	if (PC->WasInputKeyJustPressed(EKeys::G) && ToyLilliput::IsEnabled())
	{
		ToggleBody();
	}

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
		bTeleportValid = UpdateTeleportAim(Origin + Direction * 30.f * ToyLilliput::GetScale(), (Direction + FVector(0.f, 0.f, 0.25f)).GetSafeNormal(), VROrigin);
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

void AToyBoxPawn::SpawnLilliput()
{
	if (!ToyLilliput::IsEnabled())
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 小人偶：开局站在站立点前方 45cm、偏右 25cm 的地上，面朝玩家（找不到地就放在脚的高度）
	Figurine = AToyFigurine::Find(GetWorld());
	if (!Figurine)
	{
		const FVector Ahead = GetActorLocation() + GetActorRotation().RotateVector(FVector(45.f, 25.f, 0.f));
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ToyFigurineSpawn), false, this);
		AddIgnoredForMove(Query);
		FHitResult Hit;
		FVector Feet = Ahead;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0.f, 0.f, 100.f), Ahead - FVector(0.f, 0.f, 50.f), ECC_Visibility, Query))
		{
			Feet = Hit.ImpactPoint;
		}
		// 身体中心比脚高半个身高（5cm），再抬 1cm：地毯的可见性射线比它的物理碰撞面低半厘米左右，贴着放会嵌进去、一弹就倒
		Figurine = GetWorld()->SpawnActor<AToyFigurine>(AToyFigurine::StaticClass(),
			FTransform(FRotator(0.f, GetActorRotation().Yaw + 180.f, 0.f), Feet + FVector(0.f, 0.f, 6.f)), Params);
	}
	GiantBody = GetWorld()->SpawnActor<AToyGiantBody>(AToyGiantBody::StaticClass(), GetActorTransform(), Params);
	UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：小人偶 %s 在 %s"), *GetNameSafe(Figurine),
		Figurine ? *Figurine->GetActorLocation().ToCompactString() : TEXT("-"));
}

void AToyBoxPawn::ToggleBody()
{
	if (bSwitchingBody || bTeleporting || bCalibMode)
	{
		return;
	}
	if (!Figurine || !GiantBody)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox 小人国：没有小人偶（toy.Lilliput 是 0？），不能换身体"));
		return;
	}
	bSwitchingBody = true;
	bTeleportAiming = false;
	HideTeleportAim();
	APlayerController* PC = Cast<APlayerController>(GetController());
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	// 换身体时黑一下：连续缩放的过程本身容易晕（视野在变、身体没动），淡出淡入最稳
	const float Fade = TeleportFadeTime * 2.f;
	if (CameraManager)
	{
		CameraManager->StartCameraFade(0.f, 1.f, Fade, FLinearColor::Black, false, true);
	}
	TWeakObjectPtr<APlayerCameraManager> WeakCamera(CameraManager);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakCamera, Fade]()
	{
		LeftHand->ReleaseHeld(false);
		RightHand->ReleaseHeld(false);
		const float OldScale = ToyLilliput::GetScale();
		if (!bTiny)
		{
			// 大 → 小：大身体留在原地变成人像，自己出现在小人偶脚下，朝向不变
			BigFeet = GetFeetLocation();
			BigRotation = GetActorRotation();
			BigEyeHeight = Camera->GetComponentLocation().Z - GetActorLocation().Z;
			const FVector Target = Figurine->GetFeetLocation();
			Figurine->Hide();
			GiantBody->ShowAt(BigFeet, Camera->GetComponentRotation().Yaw, BigEyeHeight);
			bTiny = true;
			ApplyPlayerScale(ToyLilliput::TinyScale);
			PlaceFeetAt(Target, BigRotation, ToyLilliput::TinyScale / OldScale);
			LastGroundFeet = Target;
			FallVelocity = 0.f;
			ShowToast(TEXT("小人国 · 1/12"), TEXT("左手 Y：变回大身体\n左手 X：菜单"));
			UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：变小，站在 %s；大身体留在 %s"), *Target.ToCompactString(), *BigFeet.ToCompactString());
		}
		else
		{
			// 小 → 大：回到大身体原来的位置和朝向，小人偶留在小身体最后站的地方
			const FVector TinyFeet = GetFeetLocation();
			const float TinyYaw = Camera->GetComponentRotation().Yaw;
			GiantBody->Hide();
			bTiny = false;
			ApplyPlayerScale(1.f);
			PlaceFeetAt(BigFeet, BigRotation, 1.f / OldScale);
			FallVelocity = 0.f;
			Figurine->StandAt(TinyFeet, TinyYaw);
			ShowToast(TEXT("大身体"), TEXT("把小人偶捏起来放到想去的地方，\n再按左手 Y 变成它"));
			UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：变大，小人偶留在 %s"), *TinyFeet.ToCompactString());
		}
		if (APlayerCameraManager* Manager = WeakCamera.Get())
		{
			Manager->StartCameraFade(1.f, 0.f, Fade, FLinearColor::Black, false, false);
		}
		bSwitchingBody = false;
	}), FMath::Max(0.01f, Fade), false);
}

void AToyBoxPawn::ApplyPlayerScale(float Scale)
{
	ToyLilliput::SetScale(GetWorld(), Scale);
	if (bVR)
	{
		ApplyHandCalib();
	}
	else
	{
		Camera->SetRelativeLocation(FVector(0.f, 0.f, DesktopEyeHeight * Scale));
		LeftHand->SetActorScale3D(FVector(Scale));
		RightHand->SetActorScale3D(FVector(Scale));
		DesktopHoldDistance = FMath::Clamp(DesktopHoldDistance * Scale / FMath::Max(AppliedHandScale, KINDA_SMALL_NUMBER), 25.f * Scale, 200.f);
		AppliedHandScale = Scale;
	}
	// 开着的菜单是按原来的大小摆的：收起来，再叫出来时按新比例摆
	for (TActorIterator<AToyMenuPanel> It(GetWorld()); It; ++It)
	{
		if (It->IsOpen())
		{
			It->Close();
		}
	}
}

void AToyBoxPawn::PlaceFeetAt(const FVector& Target, const FRotator& Rotation, float HeadRatio)
{
	SetActorRotation(FRotator(0.f, Rotation.Yaw, 0.f));
	// 头相对 VROrigin 的水平偏移（VR 里人可能站在游玩区域的边上）；桌面模式头就在 VROrigin 正上方
	FVector Head = bVR ? Camera->GetRelativeLocation() * HeadRatio : FVector::ZeroVector;
	Head.Z = 0.f;
	SetActorLocation(Target - GetActorRotation().RotateVector(Head));
}

void AToyBoxPawn::TickTinyGround(float DeltaSeconds)
{
	if (bTeleporting || bSwitchingBody)
	{
		return;
	}
	// 缩小后的重力：小人眼里世界放大了 12 倍，用真实重力的话自己从桌上掉下来像从 9 米高极速坠落；乘比例后在小人眼里是正常的下落
	const float Scale = ToyLilliput::GetScale();
	const float Gravity = -GetWorld()->GetGravityZ() * Scale;
	const float MaxFallSpeed = 1500.f * Scale;
	const float StepUp = CapsuleLift * Scale;
	const float Radius = CapsuleRadius * Scale * 0.5f;
	const float NextVelocity = FMath::Max(FallVelocity - Gravity * DeltaSeconds, -MaxFallSpeed);
	const float Drop = FMath::Max(2.f * Scale, -NextVelocity * DeltaSeconds);

	// 一个小球从脚上方一个台阶高的地方往下扫到这一帧会掉到的地方：扫到朝上的面就站上去（台阶以内的高低差直接跨）
	const FVector Feet = GetFeetLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTinyGround), false, this);
	AddIgnoredForMove(Params);
	FHitResult Hit;
	const FVector From = Feet + FVector(0.f, 0.f, StepUp + Radius);
	const FVector To = Feet + FVector(0.f, 0.f, Radius - Drop);
	if (GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Radius), Params))
	{
		if (Hit.bStartPenetrating)
		{
			FallVelocity = 0.f;   // 脚上方就嵌在东西里（钻到家具底下）：不动，等走出来
			return;
		}
		if (Hit.ImpactNormal.Z > 0.5f)
		{
			const float GroundZ = Hit.Location.Z - Radius;
			if (FallVelocity < -300.f * Scale)
			{
				PlayHaptic(EControllerHand::Left, 0.5f, 0.06f);
				PlayHaptic(EControllerHand::Right, 0.5f, 0.06f);
			}
			AddActorWorldOffset(FVector(0.f, 0.f, GroundZ - Feet.Z));
			FallVelocity = 0.f;
			LastGroundFeet = FVector(Feet.X, Feet.Y, GroundZ);
			return;
		}
	}
	FallVelocity = NextVelocity;
	AddActorWorldOffset(FVector(0.f, 0.f, FallVelocity * DeltaSeconds));
	// 掉出了世界（从缝里漏下去）：回到上次站稳的地方
	if (GetFeetLocation().Z < LastGroundFeet.Z - 500.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox 小人国：掉出世界了，回到 %s"), *LastGroundFeet.ToCompactString());
		PlaceFeetAt(LastGroundFeet, GetActorRotation(), 1.f);
		FallVelocity = 0.f;
	}
}

void AToyBoxPawn::ShowToast(const FString& Title, const FString& Body, float Seconds)
{
	if (!RightHand || bCalibMode)
	{
		return;
	}
	RightHand->ShowCustomInfo(FText::FromString(Title), FText::FromString(Body));
	GetWorldTimerManager().SetTimer(ToastTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (RightHand && !bCalibMode)
		{
			RightHand->HideCustomInfo();
		}
	}), Seconds, false);
}
