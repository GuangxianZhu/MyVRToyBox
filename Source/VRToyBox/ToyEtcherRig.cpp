#include "ToyEtcherRig.h"

#include "ToyEtchPanels.h"
#include "ToyExplodedModel.h"
#include "ToyShapeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

#include <algorithm>

namespace
{
	// Tools/create_toy_materials.py 建的材质。没有的时候退回能用的：不受光照 / 受光照 → 引擎基础材质，辉光 → 半透明虚影
	const TCHAR* GhostPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	const TCHAR* UnlitPath = TEXT("/Game/Materials/M_ToyUnlit.M_ToyUnlit");
	const TCHAR* LitPath = TEXT("/Game/Materials/M_ToyLit.M_ToyLit");
	const TCHAR* GlowPath = TEXT("/Game/Materials/M_ToyGlow.M_ToyGlow");
	const TCHAR* GlowXRayPath = TEXT("/Game/Materials/M_ToyGlowXRay.M_ToyGlowXRay");
	const TCHAR* BasicPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	// 网页版工艺腔的尺寸（米，app.js 里的常数）：晶圆表面 H，内衬内半径 RIN，介质窗下表面 YTOP，晶圆表面 YW，鞘层上沿 SHEATH
	constexpr float H = 0.95f;
	constexpr float RIN = 0.285f;
	constexpr float YTOP = 1.193f;
	constexpr float YW = H + 0.002f;
	constexpr float SHEATH = H + 0.03f;
	constexpr int32 PMAX = 1400;
	constexpr float Pi = UE_PI;

	// 放大腔：真实尺寸的 60%（1 米画成 60cm），从网页坐标 Y = 0.76（粒子在这以下就算被抽走）画到喷嘴顶 1.30
	constexpr double MagK = 60.0;
	constexpr double MagY0 = 0.76;
	// 放大腔在模型支架（ModelAnchor）坐标里的位置：模型左边，腔底离桌面 30cm
	const FVector MagCenter(14.f, -54.f, 30.f);
	// 放大腔里的晶圆：放在卡盘上时中心离 MagRoot 11.3cm，升降针最多把它抬高 0.027m，机械手的手指上比卡盘上高 0.024m
	constexpr float MagWaferZ = 11.3f;

	// 桌上小模型（模型坐标，厘米，未缩放 = 真实尺寸）：闸阀门开的时候往下 12cm，升降针顶起 3.1cm，晶圆被针抬起 2.7cm（和网页一样）
	constexpr float SlitDrop = 12.f;
	constexpr float PinRise = 3.1f;
	constexpr float WaferLift = 2.7f;
	// 真空机械手的手指：Arm = 0 时晶圆中心在闸阀外 75cm（模型 +Y 是闸阀那边），1 时对正卡盘；手指上的晶圆比放在卡盘上高 2.35cm
	constexpr float ArmOutY = 75.f;
	constexpr float BladeWaferZ = 2.35f;
	constexpr float WaferRestZ = 95.05f;

	const FLinearColor BladeColor(0.55f, 0.58f, 0.62f);

	// 面板在 ModelAnchor 坐标里的位置（厘米）
	const FVector InfoBoardAt(48.f, 0.f, 92.f);
	const FVector ProfileBoardAt(26.f, -62.f, 82.f);
	const FVector ResultBoardAt(36.f, 64.f, 90.f);
	const FVector LegendAt(-9.f, -54.f, 22.f);

	float Rnd() { return FMath::FRand() * 2.f - 1.f; }

	FString Num(double Value, int32 Digits)
	{
		return FString::Printf(TEXT("%.*f"), Digits, Value);
	}

	FString Trim(double Value)
	{
		return FString::Printf(TEXT("%g"), Value);
	}

	const TCHAR* FallbackStepNames[ToyEtch::StepCount] = {
		TEXT("打开晶圆盒"), TEXT("取片、对准"), TEXT("放进负载锁"), TEXT("负载锁抽真空"), TEXT("送进工艺腔"),
		TEXT("吸附晶圆、通背氦"), TEXT("通气、稳压"), TEXT("起辉"), TEXT("主刻蚀（到终点）"), TEXT("过刻蚀"),
		TEXT("关射频、停气"), TEXT("去吸附"), TEXT("传出到负载锁"), TEXT("负载锁回大气"), TEXT("放回晶圆盒"),
	};

	// 调试命令 toy.Etch：和戳触屏刻蚀机页签上的按钮一样，可以连着写，桌面模式 / -ExecCmds 里测流程用
	//   toy.Etch preset 2 speed 3 auto next   → 选"偏压太高"、10 倍速、自动、开始
	//   next / auto / pause / reset / boards / speed <0~3> / preset <0~4> / field <序号> <+1|-1> / status
	void EtchCommand(const TArray<FString>& Args, UWorld* World)
	{
		AToyEtcherRig* Rig = nullptr;
		for (TActorIterator<AToyEtcherRig> It(World); It && !Rig; ++It)
		{
			Rig = *It;
		}
		if (!Rig)
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Etch 桌上没有刻蚀机（先 toy.Screen Etcher）"));
			return;
		}
		for (int32 Index = 0; Index < Args.Num(); ++Index)
		{
			const FString Arg = Args[Index].ToLower();
			auto NextInt = [&Args, &Index](int32 Fallback)
			{
				return Args.IsValidIndex(Index + 1) ? FCString::Atoi(*Args[++Index]) : Fallback;
			};
			if (Arg == TEXT("next") || Arg == TEXT("start")) Rig->StartOrNext();
			else if (Arg == TEXT("auto")) Rig->ToggleAuto();
			else if (Arg == TEXT("pause")) Rig->TogglePause();
			else if (Arg == TEXT("reset")) Rig->ResetProcess();
			else if (Arg == TEXT("boards")) Rig->ToggleBoards();
			else if (Arg == TEXT("speed")) Rig->SetSpeedIndex(NextInt(1));
			else if (Arg == TEXT("preset")) Rig->ApplyPreset(NextInt(0));
			else if (Arg == TEXT("field"))
			{
				const int32 Field = NextInt(0);
				Rig->StepField(Field, NextInt(1));
			}
			else if (Arg != TEXT("status"))
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Etch 不认识 %s"), *Args[Index]);
			}
		}
		const ToyEtch::FProcess& P = Rig->GetProcess();
		UE_LOG(LogTemp, Log, TEXT("ToyBox: toy.Etch %s · %s · 刻深 %.1f nm · 曲线 %d 点"), *Rig->StatusLine().ToString(),
			*Rig->ExplainTitle().ToString(), P.E.Dc, static_cast<int32>(P.Samples.size()));
	}

	FAutoConsoleCommandWithWorldAndArgs EtchCommandRef(TEXT("toy.Etch"),
		TEXT("刻蚀机演示：toy.Etch preset 2 speed 3 auto next（next / auto / pause / reset / boards / speed N / preset N / field N ±1 / status）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&EtchCommand));
}

AToyEtcherRig::AToyEtcherRig()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	// ---------- 桌上小模型（Setup 时挂到模型的 ModelRoot 上，坐标 = 模型坐标，厘米，未缩放） ----------
	SmallRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SmallRoot"));
	SmallRoot->SetupAttachment(Root);
	SmallRoot->SetMobility(EComponentMobility::Movable);
	// 辉光：网页版 plasma（半径 0.29m 的圆柱，从晶圆上方 1.6cm 到介质窗）+ plasmaCore（线圈下面一圈更亮的环，这里用压扁的球）
	SmallGlow = NewMesh(TEXT("SmallGlow"), SmallRoot, CylinderMesh, FVector(58.f, 58.f, 23.2f), FVector(0.f, 0.f, 108.2f));
	SmallGlowCore = NewMesh(TEXT("SmallGlowCore"), SmallRoot, SphereMesh, FVector(47.f, 47.f, 9.f), FVector(0.f, 0.f, 115.5f));
	// 真空机械手的手指（托晶圆的叉板）和手臂，从闸阀伸进来
	const float BladeZ = WaferRestZ + BladeWaferZ - 0.15f - 0.3f;
	SmallBlade = UToyShapeComponent::Create(this, TEXT("SmallBlade"), SmallRoot, EToyShape::Box, FVector(20.f, 26.f, 0.6f),
		FVector(0.f, ArmOutY, BladeZ), BladeColor, FRotator::ZeroRotator);
	SmallBladeArm = UToyShapeComponent::Create(this, TEXT("SmallBladeArm"), SmallRoot, EToyShape::Box, FVector(8.f, 48.f, 0.8f),
		FVector(0.f, ArmOutY + 37.f, BladeZ), BladeColor, FRotator::ZeroRotator);

	// ---------- 放大反应腔（MagRoot 局部坐标，厘米；Mag() 把网页坐标换算过来） ----------
	MagRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MagRoot"));
	MagRoot->SetupAttachment(Root);
	MagRoot->SetRelativeLocation(MagCenter);
	auto MagShape = [this](FName Name, EToyShape Shape, const FVector& Size, const FVector& Location, const FLinearColor& Color)
	{
		UToyShapeComponent* Comp = UToyShapeComponent::Create(this, Name, MagRoot, Shape, Size, Location, Color, FRotator::ZeroRotator);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Comp;
	};
	const double K = MagK;
	// 静电卡盘（顶面在 Y = 0.945）、石英绝缘环、硅边缘环
	MagEsc = MagShape(TEXT("MagEsc"), EToyShape::Cylinder, FVector(0.336 * K, 0.336 * K, 11.1), FVector(0.f, 0.f, 5.55f), FLinearColor(0.2f, 0.21f, 0.23f));
	MagInsRing = MagShape(TEXT("MagInsRing"), EToyShape::Cylinder, FVector(0.38 * K, 0.38 * K, 6.f), FVector(0.f, 0.f, 6.6f), FLinearColor(0.8f, 0.8f, 0.82f));
	MagEdgeRing = MagShape(TEXT("MagEdgeRing"), EToyShape::Cylinder, FVector(0.37 * K, 0.37 * K, 0.65f), FVector(0.f, 0.f, 10.825f), FLinearColor(0.1f, 0.1f, 0.12f));
	MagWafer = MagShape(TEXT("MagWafer"), EToyShape::Cylinder, FVector(0.30 * K, 0.30 * K, 0.3f), FVector(0.f, 0.f, MagWaferZ), FLinearColor(0.35f, 0.25f, 0.1f));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(90.f + 120.f * Index);
		MagPins.Add(MagShape(*FString::Printf(TEXT("MagPin%d"), Index), EToyShape::Cylinder, FVector(0.5f, 0.5f, 2.1f),
			FVector(4.5f * FMath::Cos(Angle), 4.5f * FMath::Sin(Angle), 10.05f), FLinearColor(0.9f, 0.88f, 0.8f)));
	}
	MagNozzle = MagShape(TEXT("MagNozzle"), EToyShape::Cylinder, FVector(3.f, 3.f, 6.6f), FVector(0.f, 0.f, 29.1f), FLinearColor(0.85f, 0.84f, 0.8f));
	// 机械手的手指在放大腔里也画：从玩家这边（-X）伸进来
	MagBlade = MagShape(TEXT("MagBlade"), EToyShape::Box, FVector(15.6f, 12.f, 0.36f), FVector(-28.f, 0.f, 12.41f), BladeColor);
	MagBladeArm = MagShape(TEXT("MagBladeArm"), EToyShape::Box, FVector(24.f, 4.8f, 0.4f), FVector(-48.f, 0.f, 12.41f), BladeColor);
	// 半透明的件（Setup 时换材质）：介质窗、约束环
	MagWindow = NewMesh(TEXT("MagWindow"), MagRoot, CylinderMesh, FVector(0.69 * K, 0.69 * K, 2.1), FVector(0.f, 0.f, 27.15f));
	MagBaffle = NewMesh(TEXT("MagBaffle"), MagRoot, CylinderMesh, FVector(0.60 * K, 0.60 * K, 0.6), FVector(0.f, 0.f, 6.f));
	// 腔底（网页坐标 Y = 0.76，粒子到这里就算被分子泵抽走）：约束环下面的抽气区也围起来，粒子不会看着像掉出腔外
	MagFloor = NewMesh(TEXT("MagFloor"), MagRoot, CylinderMesh, FVector(0.61 * K, 0.61 * K, 0.5), FVector(0.f, 0.f, 0.25f));
	// 内衬只画背面半圈（17 条竖板），前面敞开看里面；线圈是四圈铜带（小方块拼的）
	MagLiner = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MagLiner"));
	MagLiner->SetupAttachment(MagRoot);
	MagLiner->SetStaticMesh(CubeMesh);
	MagCoil = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MagCoil"));
	MagCoil->SetupAttachment(MagRoot);
	MagCoil->SetStaticMesh(CubeMesh);
	for (UInstancedStaticMeshComponent* Ism : { MagLiner.Get(), MagCoil.Get() })
	{
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(false);
	}
	// 辉光和腔里的紫光
	MagGlow = NewMesh(TEXT("MagGlow"), MagRoot, CylinderMesh, FVector(0.58 * K, 0.58 * K, 13.92), FVector(0.f, 0.f, 19.32f));
	MagGlowCore = NewMesh(TEXT("MagGlowCore"), MagRoot, SphereMesh, FVector(0.47 * K, 0.47 * K, 5.4), FVector(0.f, 0.f, 23.7f));
	MagLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MagLight"));
	MagLight->SetupAttachment(MagRoot);
	MagLight->SetRelativeLocation(FVector(0.f, 0.f, 19.2f));
	MagLight->SetMobility(EComponentMobility::Movable);
	MagLight->IntensityUnits = ELightUnits::Lumens;
	MagLight->Intensity = 0.f;
	MagLight->AttenuationRadius = 60.f;
	MagLight->SetLightColor(FLinearColor::FromSRGBColor(FColor(0xa5, 0x8c, 0xff)));
	MagLight->SetCastShadows(false);
	// 粒子：每种一个实例化网格（小球）
	for (int32 Type = 0; Type < 5; ++Type)
	{
		UInstancedStaticMeshComponent* Ism = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("Particles%d"), Type));
		Ism->SetupAttachment(MagRoot);
		Ism->SetStaticMesh(SphereMesh);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(false);
		ParticleMeshes.Add(Ism);
	}
	// 放大腔的支柱（站在桌面上，顶端接卡盘底）
	Pedestal = UToyShapeComponent::Create(this, TEXT("Pedestal"), Root, EToyShape::Cylinder, FVector(4.f, 4.f, MagCenter.Z),
		FVector(MagCenter.X, MagCenter.Y, MagCenter.Z * 0.5f), FLinearColor(0.08f, 0.08f, 0.09f), FRotator::ZeroRotator);
	PedestalBase = UToyShapeComponent::Create(this, TEXT("PedestalBase"), Root, EToyShape::Cylinder, FVector(16.f, 16.f, 1.f),
		FVector(MagCenter.X, MagCenter.Y, 0.5f), FLinearColor(0.08f, 0.08f, 0.09f), FRotator::ZeroRotator);

	// ---------- 面板 ----------
	InfoBoard = NewBoard(TEXT("InfoBoard"), FVector2D(56.f, 38.f), 20.f);
	ProfileBoard = NewBoard(TEXT("ProfileBoard"), FVector2D(30.f, 27.f), 30.f);
	ResultBoard = NewBoard(TEXT("ResultBoard"), FVector2D(34.f, 30.f), 24.f);
	LegendBoard = NewBoard(TEXT("LegendBoard"), FVector2D(30.f, 7.5f), 20.f);
}

UStaticMeshComponent* AToyEtcherRig::NewMesh(FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& SizeCm, const FVector& Location)
{
	UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Comp->SetupAttachment(Parent);
	Comp->SetStaticMesh(Mesh);
	Comp->SetMobility(EComponentMobility::Movable);
	// 基础形状都是 100 单位大小、中心在原点
	Comp->SetRelativeScale3D(SizeCm / 100.f);
	Comp->SetRelativeLocation(Location);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	return Comp;
}

UWidgetComponent* AToyEtcherRig::NewBoard(FName Name, const FVector2D& SizeCm, float PxPerCm)
{
	// 和触屏一样：WidgetComponent 的本地单位是像素，缩放 1/PxPerCm 后正好是 SizeCm 那么大，面朝 +X
	UWidgetComponent* Board = CreateDefaultSubobject<UWidgetComponent>(Name);
	Board->SetupAttachment(Root);
	Board->SetRelativeScale3D(FVector(1.f / PxPerCm));
	Board->SetWidgetSpace(EWidgetSpace::World);
	Board->SetDrawSize(SizeCm * PxPerCm);
	Board->SetDrawAtDesiredSize(false);
	Board->SetPivot(FVector2D(0.5f, 0.5f));
	Board->SetTwoSided(false);
	Board->SetBlendMode(EWidgetBlendMode::Opaque);
	Board->SetWindowFocusable(false);
	Board->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Board->SetCastShadow(false);
	return Board;
}

void AToyEtcherRig::BeginPlay()
{
	Super::BeginPlay();
	Content.Load(FPaths::ProjectContentDir() / TEXT("Etcher/etcher_content.json"));
	if (Content.Presets.Num() > 0)
	{
		Process.SetRecipe(Content.Presets[0].R);
	}
	Process.Reset();
	BuildMaterials();
	MapDot = FVector2D(-0.55, 2.40);

	// 全是看的东西：不挡手、不挡鼠标射线、不挡玩家走路
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AToyEtcherRig::Setup(AToyExplodedModel* InModel, USceneComponent* Anchor, const FVector& PlayerEye)
{
	Model = InModel;
	if (Anchor)
	{
		AttachToComponent(Anchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	if (InModel && InModel->ModelRoot)
	{
		SmallRoot->AttachToComponent(InModel->ModelRoot, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}

	BuildBoards();
	AimAt(PlayerEye);
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 刻蚀机演示模式就绪（讲解文字%s，%d 个预置配方）"), Content.bLoaded ? TEXT("已读") : TEXT("没读到"), Content.Presets.Num());
}

void AToyEtcherRig::AimAt(const FVector& PlayerEye)
{
	// 放大腔转到 +X 背对玩家：背面半圈内衬在远处，机械手从玩家这边伸进来
	const FVector Center = MagRoot->GetComponentLocation();
	const FVector Away = FVector(Center.X - PlayerEye.X, Center.Y - PlayerEye.Y, 0.f).GetSafeNormal();
	MagRoot->SetWorldRotation(FRotator(0.f, Away.Rotation().Yaw, 0.f));
	// 面板正面（WidgetComponent 的 +X）朝着玩家眼睛
	for (UWidgetComponent* Board : { InfoBoard.Get(), ProfileBoard.Get(), ResultBoard.Get(), LegendBoard.Get() })
	{
		Board->SetWorldRotation((PlayerEye - Board->GetComponentLocation()).Rotation());
	}
}

void AToyEtcherRig::BuildMaterials()
{
	UMaterialInterface* Ghost = LoadObject<UMaterialInterface>(nullptr, GhostPath);
	UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, UnlitPath);
	UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, LitPath);
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, GlowPath);
	UMaterialInterface* XRay = LoadObject<UMaterialInterface>(nullptr, GlowXRayPath);
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, BasicPath);
	if (!Unlit || !Lit || !Glow || !XRay)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 缺 M_ToyUnlit / M_ToyLit / M_ToyGlow / M_ToyGlowXRay（跑一次 Tools/create_toy_materials.py），辉光和粒子先用替代材质"));
	}
	const bool bUnlit = Unlit != nullptr;
	Unlit = Unlit ? Unlit : Basic;
	Lit = Lit ? Lit : Basic;
	Glow = Glow ? Glow : Ghost;
	XRay = XRay ? XRay : Glow;

	auto MakeMid = [this](UMaterialInterface* Parent, const FLinearColor& Color, float Opacity) -> UMaterialInstanceDynamic*
	{
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return Mid;
	};

	// 半透明的腔：内衬（淡蓝灰）、介质窗（白）、约束环（深灰）
	if (UMaterialInstanceDynamic* Liner = MakeMid(Ghost, FLinearColor(0.35f, 0.5f, 0.65f) * 4.f, 0.14f))
	{
		MagLiner->SetMaterial(0, Liner);
	}
	if (UMaterialInstanceDynamic* Window = MakeMid(Ghost, FLinearColor(0.9f, 0.9f, 0.95f) * 4.f, 0.2f))
	{
		MagWindow->SetMaterial(0, Window);
	}
	if (UMaterialInstanceDynamic* Baffle = MakeMid(Ghost, FLinearColor(0.4f, 0.45f, 0.5f) * 3.f, 0.3f))
	{
		MagBaffle->SetMaterial(0, Baffle);
		MagFloor->SetMaterial(0, Baffle);
	}
	if (UMaterialInstanceDynamic* Copper = MakeMid(Lit, FLinearColor(0.75f, 0.35f, 0.12f), 1.f))
	{
		MagCoil->SetMaterial(0, Copper);
	}

	// 辉光：网页版 0x8f6cff，透明度每帧按等离子体强度改（Opacity）
	const FLinearColor Purple = FLinearColor::FromSRGBColor(FColor(0x8f, 0x6c, 0xff)) * EmissiveBoost;
	GlowMaterials.Reset();
	GlowMaterials.Add(MakeMid(XRay, Purple, 0.f));
	GlowMaterials.Add(MakeMid(XRay, Purple, 0.f));
	GlowMaterials.Add(MakeMid(Glow, Purple, 0.f));
	GlowMaterials.Add(MakeMid(Glow, Purple, 0.f));
	UStaticMeshComponent* GlowMeshes[] = { SmallGlow, SmallGlowCore, MagGlow, MagGlowCore };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (GlowMaterials[Index])
		{
			GlowMeshes[Index]->SetMaterial(0, GlowMaterials[Index]);
		}
		GlowMeshes[Index]->SetVisibility(false);
	}

	// 粒子：不受光照的纯色小球（没有 M_ToyUnlit 时退回受光照的基础材质，颜色不乘亮度）
	for (int32 Type = 0; Type < ParticleMeshes.Num(); ++Type)
	{
		const FLinearColor Color = ToyEtchUI::ParticleColor(Type) * (bUnlit ? EmissiveBoost : 1.f);
		if (UMaterialInstanceDynamic* Mid = MakeMid(Unlit, Color, 1.f))
		{
			ParticleMeshes[Type]->SetMaterial(0, Mid);
		}
	}

	// 内衬竖板：背面半圈，半径 0.29m，从约束环（0.865）到介质窗（1.195）；下面接一圈腔壁（半径 0.30m）围住抽气区，一直到腔底（0.76）
	const double K = MagK;
	auto AddStaves = [this, K](double Radius, double Y0, double Y1)
	{
		const float R = static_cast<float>(Radius * K);
		const float Z0 = static_cast<float>((Y0 - MagY0) * K);
		const float Z1 = static_cast<float>((Y1 - MagY0) * K);
		for (int32 Index = 0; Index <= 16; ++Index)
		{
			const float Angle = -90.f + 180.f * Index / 16.f;
			const float Rad = FMath::DegreesToRadians(Angle);
			const FVector Location(R * FMath::Cos(Rad), R * FMath::Sin(Rad), (Z0 + Z1) * 0.5f);
			MagLiner->AddInstance(FTransform(FRotator(0.f, Angle, 0.f), Location, FVector(0.4f, 3.5f, Z1 - Z0) / 100.f));
		}
	};
	AddStaves(0.29, 0.865, 1.195);
	AddStaves(0.305, MagY0, 0.865);
	// 线圈：介质窗上面四圈，内两圈、外两圈
	const float CoilZ = static_cast<float>((1.245 - MagY0) * K);
	for (const float RingR : { 0.09f, 0.12f, 0.20f, 0.235f })
	{
		const float R = static_cast<float>(RingR * K);
		const int32 Count = FMath::Max(12, FMath::RoundToInt(2.f * Pi * R / 1.2f));
		const float Segment = 2.f * Pi * R / Count * 1.08f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = 360.f * Index / Count;
			const float Rad = FMath::DegreesToRadians(Angle);
			MagCoil->AddInstance(FTransform(FRotator(0.f, Angle + 90.f, 0.f), FVector(R * FMath::Cos(Rad), R * FMath::Sin(Rad), CoilZ),
				FVector(Segment, 0.72f, 0.72f) / 100.f));
		}
	}
}

void AToyEtcherRig::BuildBoards()
{
	Brushes = MakeShared<FToyEtchBrushes>();
	const TWeakObjectPtr<AToyEtcherRig> Weak(this);
	struct FBoardSpec
	{
		UWidgetComponent* Board;
		FVector At;
		TSharedRef<SWidget> Widget;
	};
	const FBoardSpec Specs[] = {
		{ InfoBoard, InfoBoardAt, ToyEtchUI::MakeInfoBoard(Weak, *Brushes) },
		{ ProfileBoard, ProfileBoardAt, ToyEtchUI::MakeProfileBoard(Weak, *Brushes) },
		{ ResultBoard, ResultBoardAt, ToyEtchUI::MakeResultBoard(Weak, *Brushes) },
		{ LegendBoard, LegendAt, ToyEtchUI::MakeLegend(Weak, *Brushes) },
	};
	for (const FBoardSpec& Spec : Specs)
	{
		// 面板挂在 Root（= ModelAnchor）下，朝向在 AimAt 里摆
		Spec.Board->SetRelativeLocation(Spec.At);
		Spec.Board->SetTintColorAndOpacity(FLinearColor(BoardBrightness, BoardBrightness, BoardBrightness, 1.f));
		Spec.Board->SetSlateWidget(Spec.Widget);
	}
}

void AToyEtcherRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 小模型的零件摆回原位（模型可能还在：只是工作室换了别的东西）
	if (AToyExplodedModel* M = Model.Get())
	{
		M->SetUnitPose(TEXT("K09_SlitDoor"), FVector::ZeroVector, true);
		M->SetUnitPose(TEXT("K16_LiftPins"), FVector::ZeroVector, true);
		M->SetUnitPose(TEXT("K12_Wafer"), FVector::ZeroVector, true);
	}
	for (UWidgetComponent* Board : { InfoBoard.Get(), ProfileBoard.Get(), ResultBoard.Get(), LegendBoard.Get() })
	{
		if (Board)
		{
			Board->SetSlateWidget(nullptr);
		}
	}
	Super::EndPlay(EndPlayReason);
}

float AToyEtcherRig::SpeedValue(int32 Index)
{
	static const float Speeds[SpeedCount] = { 1.f, 2.f, 4.f, 10.f };
	return Speeds[FMath::Clamp(Index, 0, SpeedCount - 1)];
}

bool AToyEtcherRig::IsAssembled() const
{
	const AToyExplodedModel* M = Model.Get();
	return M && !M->IsExploded() && M->GetProgress() <= 0.f;
}

void AToyEtcherRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Model.IsValid())
	{
		return;
	}
	// 和网页一样：真实时间每帧最多算 0.05 秒，再乘倍速；工艺内部再切成 0.05 秒的小步
	const float Real = FMath::Min(DeltaSeconds, 0.05f);
	Process.Advance(bPaused ? 0.0 : Real * SpeedValue(SpeedIndex));
	if (bNeedAssemble && IsAssembled())
	{
		bNeedAssemble = false;
	}
	UpdateSmallModel();
	UpdateMagnified(bPaused ? 0.f : Real);
	InfoBoard->SetVisibility(bBoardsShown);
	ProfileBoard->SetVisibility(bBoardsShown);
	ResultBoard->SetVisibility(bBoardsShown && Process.bHasResult);
	UpdateProfileDots(Real);
	UpdateMapDot(Real);
}

void AToyEtcherRig::UpdateSmallModel()
{
	AToyExplodedModel* M = Model.Get();
	const ToyEtch::FToolState& S = Process.S;
	const bool bAssembled = IsAssembled();
	const bool bRunning = Process.StepIdx >= 0 && !Process.bDone;

	M->SetUnitPose(TEXT("K09_SlitDoor"), FVector(0.f, 0.f, -SlitDrop * S.Slit), true);
	M->SetUnitPose(TEXT("K16_LiftPins"), FVector(0.f, 0.f, PinRise * S.PinH), true);
	// 晶圆：拆开的时候在原位（可以抓起来看）；工艺腔里没有晶圆时藏起来（它在晶圆盒、负载锁、传输腔里）
	bool bWafer = !bAssembled;
	FVector WaferOffset = FVector::ZeroVector;
	if (bAssembled && bRunning)
	{
		if (S.Holder == ToyEtch::EHolder::Chamber)
		{
			bWafer = true;
			WaferOffset = FVector(0.f, 0.f, WaferLift * S.PinH);
		}
		else if (S.Holder == ToyEtch::EHolder::VtmRobot && S.bArm)
		{
			bWafer = true;
			WaferOffset = FVector(0.f, ArmOutY * (1.f - S.Arm), BladeWaferZ);
		}
	}
	M->SetUnitPose(TEXT("K12_Wafer"), WaferOffset, bWafer);

	const bool bBlade = bAssembled && S.bArm;
	const float BladeY = ArmOutY * static_cast<float>(1.0 - S.Arm);
	SmallBlade->SetVisibility(bBlade);
	SmallBladeArm->SetVisibility(bBlade);
	if (bBlade)
	{
		const float BladeZ = WaferRestZ + BladeWaferZ - 0.45f;
		SmallBlade->SetRelativeLocation(FVector(0.f, BladeY, BladeZ));
		SmallBladeArm->SetRelativeLocation(FVector(0.f, BladeY + 37.f, BladeZ));
	}

	// 辉光：网页版 f = 等离子体强度 × 密度修正 × 一点闪烁；小模型隔着腔壁看，淡一点
	const bool bGlow = bAssembled && S.Plasma > 0.01;
	const float F = static_cast<float>(S.Plasma * FMath::Clamp(0.35 + Process.M.n * 0.6, 0.3, 1.3)) * (0.93f + 0.07f * FMath::FRand());
	SmallGlow->SetVisibility(bGlow);
	SmallGlowCore->SetVisibility(bGlow);
	if (bGlow && GlowMaterials.Num() >= 2)
	{
		if (GlowMaterials[0]) GlowMaterials[0]->SetScalarParameterValue(TEXT("Opacity"), 0.12f * F);
		if (GlowMaterials[1]) GlowMaterials[1]->SetScalarParameterValue(TEXT("Opacity"), 0.22f * F);
	}
}

FVector AToyEtcherRig::Mag(double X, double Y, double Z) const
{
	return FVector(X * MagK, Z * MagK, (Y - MagY0) * MagK);
}

void AToyEtcherRig::UpdateMagnified(float Dt)
{
	const ToyEtch::FToolState& S = Process.S;
	const ToyEtch::FEtch& E = Process.E;
	// 拆开时零件会飞到左边这块地方，放大腔整个收起来（网页版拆开时也不画粒子）
	const bool bShow = bBoardsShown && IsAssembled();
	if (bShow != bMagShown)
	{
		// 只在切换时改：子组件（辉光、晶圆、手指）平时各自按状态显示 / 隐藏，打开时下面马上重新摆
		bMagShown = bShow;
		MagRoot->SetVisibility(bShow, true);
		Pedestal->SetVisibility(bShow);
		PedestalBase->SetVisibility(bShow);
		LegendBoard->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}
	const bool bRunning = Process.StepIdx >= 0 && !Process.bDone;

	// 晶圆、升降针、机械手的手指
	bool bWafer = false;
	FVector WaferAt(0.f, 0.f, MagWaferZ);
	if (bRunning && S.Holder == ToyEtch::EHolder::Chamber)
	{
		bWafer = true;
		WaferAt.Z += 0.027 * MagK * S.PinH;
	}
	else if (bRunning && S.Holder == ToyEtch::EHolder::VtmRobot && S.bArm)
	{
		bWafer = true;
		WaferAt = FVector(-0.47 * MagK * (1.0 - S.Arm), 0.f, MagWaferZ + 0.024 * MagK);
	}
	MagWafer->SetVisibility(bWafer);
	if (bWafer)
	{
		MagWafer->SetRelativeLocation(WaferAt);
		// 颜色：多晶硅（金黄）刻穿以后露出栅氧（蓝）
		const double Key = FMath::Clamp(E.Dc / 100.0, 0.0, 1.0) * (1.0 - E.Uncleared);
		if (FMath::Abs(Key - LastWaferColorKey) > 0.01)
		{
			LastWaferColorKey = Key;
			MagWafer->SetColor(FMath::Lerp(ToyEtchUI::Hex(0xa08a5c), ToyEtchUI::Hex(0x4f6fb0), static_cast<float>(Key)));
		}
	}
	const float PinTop = 11.1f + static_cast<float>(0.031 * MagK * S.PinH);
	for (UToyShapeComponent* Pin : MagPins)
	{
		const float Height = PinTop - 9.f;
		FVector Location = Pin->GetRelativeLocation();
		Location.Z = 9.f + Height * 0.5f;
		Pin->SetRelativeLocation(Location);
		Pin->SetRelativeScale3D(FVector(0.5f, 0.5f, Height) / 100.f);
	}
	MagBlade->SetVisibility(S.bArm);
	MagBladeArm->SetVisibility(S.bArm);
	if (S.bArm)
	{
		const float X = static_cast<float>(-0.47 * MagK * (1.0 - S.Arm));
		const float Z = MagWaferZ + static_cast<float>(0.024 * MagK) - 0.15f - 0.18f;
		MagBlade->SetRelativeLocation(FVector(X, 0.f, Z));
		MagBladeArm->SetRelativeLocation(FVector(X - 7.8f - 12.f, 0.f, Z));
	}

	// 辉光 + 紫光
	const bool bGlow = S.Plasma > 0.01;
	MagGlow->SetVisibility(bGlow);
	MagGlowCore->SetVisibility(bGlow);
	const float F = bGlow ? static_cast<float>(S.Plasma * FMath::Clamp(0.35 + Process.M.n * 0.6, 0.3, 1.3)) * (0.93f + 0.07f * FMath::FRand()) : 0.f;
	if (bGlow && GlowMaterials.Num() >= 4)
	{
		if (GlowMaterials[2]) GlowMaterials[2]->SetScalarParameterValue(TEXT("Opacity"), 0.14f * F);
		if (GlowMaterials[3]) GlowMaterials[3]->SetScalarParameterValue(TEXT("Opacity"), 0.28f * F);
	}
	MagLight->SetIntensity(300.f * F);

	// 粒子：和网页一样，晶圆在腔里（或者腔里还有粒子没抽完）才算
	if (S.Holder == ToyEtch::EHolder::Chamber || Particles.Num() > 0)
	{
		UpdateParticles(Dt);
	}
	DrawParticles();
}

bool AToyEtcherRig::InWafer(float X, float Z) const
{
	return Process.S.Holder == ToyEtch::EHolder::Chamber && Process.S.PinH < 0.05 && X * X + Z * Z < 0.15f * 0.15f;
}

FToyEtchParticle* AToyEtcherRig::Spawn(uint8 Type, const FVector3f& P, const FVector3f& V)
{
	if (Particles.Num() >= PMAX)
	{
		return nullptr;
	}
	FToyEtchParticle& Particle = Particles.AddDefaulted_GetRef();
	Particle.Type = Type;
	Particle.P = P;
	Particle.V = V;
	Particle.Age = 0.f;
	Particle.Life = 6.f + FMath::FRand() * 4.f;
	return &Particle;
}

void AToyEtcherRig::UpdateParticles(float Dt)
{
	// 照搬网页版 updateParticles()：数量、速度都是示意，不是真实比例
	if (Dt <= 0.f)
	{
		return;
	}
	const ToyEtch::FToolState& S = Process.S;
	const ToyEtch::FEtch& E = Process.E;
	const float Flow = static_cast<float>(S.Cl2 + S.HBr + S.O2);
	const float Pl = static_cast<float>(S.Plasma);
	const bool bBias = S.BiasF > 1;
	enum { Gas, Electron, Ion, Radical, Product };

	// 进气：从中心喷嘴往下喷
	SpawnGas += Dt * Flow * 0.55f;
	while (SpawnGas >= 1.f)
	{
		SpawnGas -= 1.f;
		const float A = FMath::FRand() * Pi * 2.f;
		Spawn(Gas, FVector3f(0.015f * FMath::Cos(A), YTOP - 0.005f, 0.015f * FMath::Sin(A)),
			FVector3f(0.35f * FMath::Cos(A) * FMath::FRand(), -0.35f, 0.35f * FMath::Sin(A) * FMath::FRand()));
	}
	// 电子：起辉后一直保持一群在乱窜
	int32 Electrons = 0;
	for (const FToyEtchParticle& Particle : Particles)
	{
		Electrons += Particle.Type == Electron ? 1 : 0;
	}
	for (int32 Count = Electrons; Count < FMath::RoundToInt(70.f * Pl); ++Count)
	{
		if (FToyEtchParticle* Particle = Spawn(Electron, FVector3f(Rnd() * 0.25f, H + 0.05f + FMath::FRand() * 0.2f, Rnd() * 0.25f),
			FVector3f(Rnd() * 1.6f, Rnd() * 1.6f, Rnd() * 1.6f)))
		{
			Particle->Life = 0.6f + FMath::FRand();
		}
	}

	// 新生成的产物先放一边，循环完再加进去（循环里加会让引用失效）
	TArray<FToyEtchParticle, TInlineAllocator<16>> Born;
	for (int32 Index = Particles.Num() - 1; Index >= 0; --Index)
	{
		FToyEtchParticle& Pt = Particles[Index];
		Pt.Age += Dt;
		bool bDead = Pt.Age > Pt.Life;
		const float R = FMath::Sqrt(Pt.P.X * Pt.P.X + Pt.P.Z * Pt.P.Z);
		if (Pt.Type == Gas || Pt.Type == Radical || Pt.Type == Product)
		{
			// 气流：往外、往下，穿过约束环（半径 0.19~0.30、高度 0.86）流向分子泵
			const float Out = Pt.P.Y < 1.02f ? 0.22f : 0.06f;
			const float Down = R > 0.19f ? 0.28f : 0.05f;
			const float Ux = R > 1e-4f ? Pt.P.X / R : 0.f;
			const float Uz = R > 1e-4f ? Pt.P.Z / R : 0.f;
			const float Jit = Pt.Type == Radical ? 0.9f : 0.5f;
			Pt.V.X += (Ux * Out - Pt.V.X) * Dt * 2.f + Rnd() * Jit * Dt * 6.f;
			Pt.V.Z += (Uz * Out - Pt.V.Z) * Dt * 2.f + Rnd() * Jit * Dt * 6.f;
			Pt.V.Y += (-Down - Pt.V.Y) * Dt * 2.f + Rnd() * Jit * Dt * 6.f;
			if (Pt.Type == Product && Pt.Age < 0.5f)
			{
				Pt.V.Y = FMath::Max(Pt.V.Y, 0.18f);   // 刚生成的产物先往上冒
			}
			// 起辉后气体分子被电子撞碎：变成自由基或离子
			if (Pt.Type == Gas && Pl > 0.05f && FMath::FRand() < Pl * 1.8f * Dt)
			{
				if (FMath::FRand() < 0.72f)
				{
					Pt.Type = Radical;
					Pt.Life = Pt.Age + 3.f + FMath::FRand() * 3.f;
				}
				else
				{
					Pt.Type = Ion;
					Pt.Life = Pt.Age + 4.f;
					Pt.V *= 0.3f;
				}
			}
			// 自由基碰到晶圆就吸附
			if (Pt.Type == Radical && Pt.P.Y < YW + 0.006f && InWafer(Pt.P.X, Pt.P.Z))
			{
				Adsorbed += 1.f;
				bDead = true;
			}
			if (Pt.P.Y < 0.76f)
			{
				bDead = true;
			}
		}
		else if (Pt.Type == Ion)
		{
			if (Pt.P.Y < SHEATH && (InWafer(Pt.P.X, Pt.P.Z) || R < 0.185f))
			{
				// 进了鞘层：被电场垂直加速往下打；有偏压时快得多
				Pt.V.Y -= (bBias ? 60.f : 8.f) * Dt;
				Pt.V.X *= 0.9f;
				Pt.V.Z *= 0.9f;
			}
			else
			{
				Pt.V.X += Rnd() * Dt * 1.2f;
				Pt.V.Z += Rnd() * Dt * 1.2f;
				Pt.V.Y += (Rnd() * 1.2f - 0.6f) * Dt;
				Pt.V *= 0.97f;
				if (Pl < 0.05f)
				{
					Pt.Life = FMath::Min(Pt.Life, Pt.Age + 0.3f);   // 没有等离子体，离子很快复合掉
				}
			}
			if (Pt.P.Y <= YW)
			{
				bDead = true;
				// 离子打在"吸附了自由基"的地方才生成产物；刻穿以后多晶硅没了，产物少很多
				if (InWafer(Pt.P.X, Pt.P.Z) && S.bEtching && Adsorbed > 0.f)
				{
					const float Yield = FMath::Clamp(0.25f + (bBias ? 0.6f : 0.05f), 0.f, 0.95f) * (E.Dc < 100 ? 1.f : 0.15f);
					if (FMath::FRand() < Yield && Particles.Num() + Born.Num() < PMAX)
					{
						Adsorbed -= 1.f;
						FToyEtchParticle& Product_ = Born.AddDefaulted_GetRef();
						Product_.Type = Product;
						Product_.P = FVector3f(Pt.P.X, YW + 0.003f, Pt.P.Z);
						Product_.V = FVector3f(Rnd() * 0.05f, 0.25f, Rnd() * 0.05f);
						Product_.Life = 6.f + FMath::FRand() * 4.f;
					}
				}
			}
		}
		else if (Pt.Type == Electron && Pl < 0.05f)
		{
			bDead = true;
		}
		Pt.P += Pt.V * Dt;
		// 关在腔里：碰到内衬、介质窗就弹回来
		const float R2 = FMath::Sqrt(Pt.P.X * Pt.P.X + Pt.P.Z * Pt.P.Z);
		const float RMax = Pt.P.Y > 0.87f ? RIN : 0.30f;
		if (R2 > RMax && !(Pt.P.Y < 0.875f && R2 < 0.30f))
		{
			const float Scale = RMax / R2;
			Pt.P.X *= Scale;
			Pt.P.Z *= Scale;
			Pt.V.X = -Pt.V.X * 0.5f;
			Pt.V.Z = -Pt.V.Z * 0.5f;
		}
		if (Pt.P.Y > YTOP)
		{
			Pt.P.Y = YTOP;
			Pt.V.Y = -FMath::Abs(Pt.V.Y);
		}
		if (Pt.P.Y < YW && Pt.Type != Ion && R2 < 0.185f)
		{
			Pt.P.Y = YW;
			Pt.V.Y = FMath::Abs(Pt.V.Y) * 0.5f;
		}
		if (bDead)
		{
			Particles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
	Particles.Append(Born);
	Adsorbed = FMath::Min(Adsorbed, 60.f);
	if (!S.bEtching && S.Plasma < 0.05)
	{
		Adsorbed = FMath::Max(0.f, Adsorbed - Dt * 10.f);
	}
}

void AToyEtcherRig::DrawParticles()
{
	// 每种粒子一个实例化网格，每帧整批重写。离子往下冲时拉长成一道短线（网页版的尾迹）
	TArray<FTransform> Transforms[5];
	for (const FToyEtchParticle& Pt : Particles)
	{
		const float Size = Pt.Type == 1 ? 0.5f : 0.8f;
		FVector Location = Mag(Pt.P.X, Pt.P.Y, Pt.P.Z);
		FVector Scale(Size / 100.f);
		if (Pt.Type == 2 && Pt.V.Y < -0.4f)
		{
			const float Trail = FMath::Clamp(-Pt.V.Y * 0.02f, 0.015f, 0.09f) * static_cast<float>(MagK);
			Location.Z += Trail * 0.5f;
			Scale.Z = FMath::Max(Size, Trail) / 100.f;
		}
		Transforms[FMath::Min<int32>(Pt.Type, 4)].Add(FTransform(FQuat::Identity, Location, Scale));
	}
	for (int32 Type = 0; Type < ParticleMeshes.Num(); ++Type)
	{
		UInstancedStaticMeshComponent* Ism = ParticleMeshes[Type];
		Ism->ClearInstances();
		if (Transforms[Type].Num() > 0)
		{
			Ism->AddInstances(Transforms[Type], false, false, false);
		}
	}
}

void AToyEtcherRig::UpdateProfileDots(float Dt)
{
	// 照搬网页版 drawProfile() 里的 parts2d：离子从上面打下来（有偏压时又快又直），自由基在表面附近乱飘
	const ToyEtch::FProcess& P = Process;
	const float Dtv = bPaused ? 0.f : Dt * FMath::Min(SpeedValue(SpeedIndex), 3.f);
	if (P.S.Plasma > 0.05 && Dtv > 0.f)
	{
		const float IonRate = static_cast<float>(P.S.Plasma * FMath::Clamp(P.M.n, 0.2, 1.4) * 60.0);
		const float RadRate = static_cast<float>(P.S.Plasma * FMath::Clamp(P.M.Gn, 0.1, 2.0) * 25.0);
		for (int32 N = 0; N < IonRate * Dtv; ++N)
		{
			const float Sigma = static_cast<float>(2.0 + P.R.P * 0.35) * Pi / 180.f * (P.S.BiasF > 1 ? 1.f : 4.f);
			const float A = (FMath::FRand() + FMath::FRand() + FMath::FRand() - 1.5f) * Sigma * 1.4f;
			const float V = P.S.BiasF > 1 ? 260.f + static_cast<float>(FMath::Sqrt(P.M.Ei)) * 25.f : 120.f;
			ProfileDots.Add(FToyProfileDot{ true, (FMath::FRand() - 0.5f) * ToyEtchUI::ProfileW, -5.f, FMath::Sin(A) * V, FMath::Cos(A) * V, 3.f });
		}
		for (int32 N = 0; N < RadRate * Dtv; ++N)
		{
			ProfileDots.Add(FToyProfileDot{ false, (FMath::FRand() - 0.5f) * ToyEtchUI::ProfileW, FMath::FRand() * 60.f, 0.f, 0.f, 1.5f + FMath::FRand() });
		}
	}
	for (int32 Index = ProfileDots.Num() - 1; Index >= 0; --Index)
	{
		FToyProfileDot& Dot = ProfileDots[Index];
		Dot.Life -= Dtv;
		if (Dot.bIon)
		{
			Dot.X += Dot.VX * Dtv;
			Dot.Y += Dot.VY * Dtv;
		}
		else
		{
			Dot.X += (FMath::FRand() - 0.5f) * 140.f * Dtv;
			Dot.Y += (FMath::FRand() - 0.45f) * 140.f * Dtv;
		}
		if (Dot.Life <= 0.f || Dot.Y > ToyEtchUI::ProfileSurface(*this, Dot.X) || Dot.Y > ToyEtchUI::ProfileH || FMath::Abs(Dot.X) > ToyEtchUI::ProfileW * 0.5f)
		{
			ProfileDots.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
	if (ProfileDots.Num() > 1500)
	{
		ProfileDots.RemoveAt(0, ProfileDots.Num() - 1500);
	}
}

void AToyEtcherRig::UpdateMapDot(float Dt)
{
	// 平台俯视坐标（米，网页版 POS）：晶圆盒 (-0.55, 2.40)、对准器 (0.70, 1.80)、负载锁 (-0.32, 1.0)、传输腔 (0, 0)、PM1 (0, -1.05)
	const ToyEtch::FToolState& S = Process.S;
	FVector2D Target(-0.55, 2.40);
	switch (S.Holder)
	{
	case ToyEtch::EHolder::AtmRobot: Target = FVector2D(0.0, 1.80); break;
	case ToyEtch::EHolder::Aligner:  Target = FVector2D(0.70, 1.80); break;
	case ToyEtch::EHolder::LoadLock: Target = FVector2D(-0.32, 1.0); break;
	case ToyEtch::EHolder::VtmRobot: Target = S.bArm ? FVector2D(0.0, -0.3 - 0.75 * S.Arm) : FVector2D(0.0, 0.0); break;
	case ToyEtch::EHolder::Chamber:  Target = FVector2D(0.0, -1.05); break;
	default: break;
	}
	MapDot = FMath::Vector2DInterpTo(MapDot, Target, Dt, 6.f);
}

// ---------- 按钮 ----------

void AToyEtcherRig::StartOrNext()
{
	// 拆开着不能开始（网页版拆开滑块和开始按钮互斥）；跑完以后按一下 = 再来一片
	if (!Process.bDone && !Process.bBusy && !IsAssembled())
	{
		bNeedAssemble = true;
		return;
	}
	const ToyEtch::EStartResult Result = Process.StartOrNext();
	switch (Result)
	{
	case ToyEtch::EStartResult::BadRecipe:
		BadProblem = Process.M.Problem;
		break;
	case ToyEtch::EStartResult::Started:
		BadProblem = ToyEtch::EProblem::None;
		bNeedAssemble = false;
		bPaused = false;
		Particles.Reset();
		ProfileDots.Reset();
		Adsorbed = 0.f;
		break;
	case ToyEtch::EStartResult::Reset:
		Particles.Reset();
		ProfileDots.Reset();
		Adsorbed = 0.f;
		break;
	default:
		break;
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 刻蚀机 %s"), *StatusLine().ToString());
}

void AToyEtcherRig::ToggleAuto()
{
	if (!Process.bAuto && !Process.bDone && !Process.bBusy && Process.StepIdx >= 0 && !IsAssembled())
	{
		bNeedAssemble = true;
		return;
	}
	Process.SetAuto(!Process.bAuto);
}

void AToyEtcherRig::TogglePause()
{
	bPaused = !bPaused;
}

void AToyEtcherRig::SetSpeedIndex(int32 Index)
{
	SpeedIndex = FMath::Clamp(Index, 0, SpeedCount - 1);
}

void AToyEtcherRig::ApplyPreset(int32 Index)
{
	if (Content.Presets.IsValidIndex(Index) && Process.SetRecipe(Content.Presets[Index].R))
	{
		BadProblem = ToyEtch::EProblem::None;
	}
}

void AToyEtcherRig::StepField(int32 Field, int32 Dir)
{
	if (!Content.Fields.IsValidIndex(Field) || Process.IsLocked())
	{
		return;
	}
	const FToyEtchField& Spec = Content.Fields[Field];
	ToyEtch::FRecipe R = Process.R;
	double& Value = FToyEtchContent::RecipeValue(R, Spec.Key);
	Value = FMath::Clamp(FMath::RoundToDouble(Value / Spec.Step + Dir) * Spec.Step, Spec.Min, Spec.Max);
	Process.SetRecipe(R);
	LastField = Field;
	BadProblem = ToyEtch::EProblem::None;
}

void AToyEtcherRig::ResetProcess()
{
	if (Process.bBusy)
	{
		return;
	}
	Process.Reset();
	Particles.Reset();
	ProfileDots.Reset();
	Adsorbed = 0.f;
	BadProblem = ToyEtch::EProblem::None;
}

void AToyEtcherRig::ToggleBoards()
{
	// 面板和放大腔的显示在 Tick 里按这个开关摆
	bBoardsShown = !bBoardsShown;
}

// ---------- 文字 ----------

TMap<FString, FString> AToyEtcherRig::FillValues() const
{
	const ToyEtch::FRecipe& R = Process.R;
	TMap<FString, FString> Values = {
		{ TEXT("P"), Trim(R.P) }, { TEXT("Ws"), Trim(R.Ws) }, { TEXT("Wb"), Trim(R.Wb) }, { TEXT("cl2"), Trim(R.Cl2) },
		{ TEXT("hbr"), Trim(R.HBr) }, { TEXT("o2"), Trim(R.O2) }, { TEXT("T"), Trim(R.T) }, { TEXT("oe"), Trim(R.Oe) },
		{ TEXT("valve"), Num(Process.ValveTarget(), 0) }, { TEXT("Ei"), Num(Process.M.Ei, 0) }, { TEXT("oeSec"), Num(Process.OverEtchSeconds(), 0) },
	};
	if (const FString* Problem = Content.Problems.Find(UTF8_TO_TCHAR(ToyEtch::ProblemKey(BadProblem))))
	{
		Values.Add(TEXT("problem"), *Problem);
	}
	return Values;
}

FText AToyEtcherRig::StepName(int32 Index) const
{
	if (Content.Steps.IsValidIndex(Index))
	{
		return FText::FromString(ToyEtchUI::VrText(Content.Steps[Index].Name));
	}
	return FText::FromString(Index >= 0 && Index < ToyEtch::StepCount ? FallbackStepNames[Index] : TEXT(""));
}

int32 AToyEtcherRig::StepState(int32 Index) const
{
	const ToyEtch::FProcess& P = Process;
	if (Index < P.StepIdx || (P.bDone && Index <= P.StepIdx) || (!P.bBusy && Index == P.StepIdx))
	{
		return 2;
	}
	return Index == P.StepIdx && P.bBusy ? 1 : 0;
}

FText AToyEtcherRig::ExplainTitle() const
{
	const ToyEtch::FProcess& P = Process;
	if (bNeedAssemble)
	{
		return FText::FromString(TEXT("先组装回去"));
	}
	if (BadProblem != ToyEtch::EProblem::None)
	{
		return FText::FromString(Content.BadTitle.IsEmpty() ? TEXT("配方检查没通过") : Content.BadTitle);
	}
	if (P.bDone)
	{
		return FText::FromString(TEXT("完成"));
	}
	if (P.StepIdx < 0)
	{
		return FText::FromString(Content.ReadyTitle.IsEmpty() ? TEXT("准备好了") : Content.ReadyTitle);
	}
	return FText::FromString(FString::Printf(TEXT("第 %d 步 · %s"), P.StepIdx + 1, *StepName(P.StepIdx).ToString()));
}

FText AToyEtcherRig::ExplainNow() const
{
	const ToyEtch::FProcess& P = Process;
	if (bNeedAssemble)
	{
		return FText::FromString(TEXT("模型现在是拆开的。跑工艺要先组装回去：点触屏上的「组装回去」，等零件都回到原位再点开始。"));
	}
	if (BadProblem != ToyEtch::EProblem::None)
	{
		return FText::FromString(ToyEtchUI::VrText(FToyEtchContent::Fill(Content.BadNow, FillValues())));
	}
	if (P.bDone)
	{
		// 网页版这句说"结果在弹窗里"，VR 里换成结果板
		return FText::FromString(TEXT("晶圆已经放回晶圆盒。结果在右边的结果板和触屏的「结果」页；剖面和曲线都留着，可以对照着看。"));
	}
	if (P.StepIdx < 0)
	{
		return FText::FromString(ToyEtchUI::VrText(Content.ReadyNow));
	}
	if (!Content.Steps.IsValidIndex(P.StepIdx))
	{
		return FText();
	}
	const FToyEtchStepText& Step = Content.Steps[P.StepIdx];
	const FString& Text = P.E.bTimeout && !Step.NowTimeout.IsEmpty() ? Step.NowTimeout : Step.Now;
	return FText::FromString(ToyEtchUI::VrText(FToyEtchContent::Fill(Text, FillValues())));
}

FText AToyEtcherRig::ExplainWhy() const
{
	const ToyEtch::FProcess& P = Process;
	if (bNeedAssemble || BadProblem != ToyEtch::EProblem::None || P.bDone)
	{
		return FText();
	}
	if (P.StepIdx < 0)
	{
		return FText::FromString(ToyEtchUI::VrText(Content.ReadyWhy));
	}
	return Content.Steps.IsValidIndex(P.StepIdx) ? FText::FromString(ToyEtchUI::VrText(Content.Steps[P.StepIdx].Why)) : FText();
}

bool AToyEtcherRig::IsExplainError() const
{
	return bNeedAssemble || BadProblem != ToyEtch::EProblem::None;
}

FText AToyEtcherRig::StartLabel() const
{
	const ToyEtch::FProcess& P = Process;
	if (P.bDone)
	{
		return FText::FromString(TEXT("再跑一片"));
	}
	if (P.bBusy)
	{
		return FText::FromString(FString::Printf(TEXT("进行中：%s"), *StepName(P.StepIdx).ToString()));
	}
	return FText::FromString(FString::Printf(TEXT("%s：%s"), P.StepIdx < 0 ? TEXT("开始") : TEXT("下一步"), *StepName(P.StepIdx + 1).ToString()));
}

bool AToyEtcherRig::CanStart() const
{
	return !Process.bBusy;
}

FText AToyEtcherRig::StatusLine() const
{
	const ToyEtch::FProcess& P = Process;
	FString State;
	if (bNeedAssemble)
	{
		State = TEXT("模型拆开着，先组装回去");
	}
	else if (P.bDone)
	{
		State = TEXT("完成");
	}
	else if (P.StepIdx < 0)
	{
		State = TEXT("待机");
	}
	else if (P.bBusy)
	{
		State = FString::Printf(TEXT("第 %d/%d 步"), P.StepIdx + 1, ToyEtch::StepCount);
	}
	else
	{
		State = FString::Printf(TEXT("等待第 %d 步"), P.StepIdx + 2);
	}
	if (bPaused)
	{
		State += TEXT("（暂停）");
	}
	return FText::FromString(FString::Printf(TEXT("%s · %s"), *State, *TimeText().ToString()));
}

FText AToyEtcherRig::TimeText() const
{
	const ToyEtch::FProcess& P = Process;
	return FText::FromString(FString::Printf(TEXT("%.1f s"), P.ProcT0 < 0 ? 0.0 : P.SimT - P.ProcT0));
}

FText AToyEtcherRig::EndpointText() const
{
	const ToyEtch::FProcess& P = Process;
	if (P.E.bHasEndpoint)
	{
		return FText::FromString(FString::Printf(TEXT("终点 @ 主刻 %.1f s"), P.E.tEp));
	}
	return FText::FromString(P.S.bEtching ? TEXT("等终点…") : TEXT(""));
}

FText AToyEtcherRig::DepthText() const
{
	const ToyEtch::FEtch& E = Process.E;
	const float SideMax = *std::max_element(E.SideCut, E.SideCut + 101);
	return FText::FromString(FString::Printf(TEXT("刻深 %.0f / 100 nm · 钻蚀 %.1f nm"), E.Dc, SideMax));
}

FText AToyEtcherRig::FieldName(int32 Field) const
{
	return Content.Fields.IsValidIndex(Field) ? FText::FromString(ToyEtchUI::VrText(Content.Fields[Field].Name)) : FText();
}

FText AToyEtcherRig::FieldValue(int32 Field) const
{
	if (!Content.Fields.IsValidIndex(Field))
	{
		return FText();
	}
	const FToyEtchField& Spec = Content.Fields[Field];
	return FText::FromString(FString::Printf(TEXT("%s %s"), *Trim(FToyEtchContent::RecipeValue(Process.R, Spec.Key)), *Spec.Unit));
}

FText AToyEtcherRig::FieldTip() const
{
	if (Process.IsLocked())
	{
		return FText::FromString(TEXT("工艺进行中，配方锁住了。跑完（或者到「流程」里等它做完）再改。"));
	}
	if (Content.Fields.IsValidIndex(LastField))
	{
		const FToyEtchField& Spec = Content.Fields[LastField];
		return FText::FromString(ToyEtchUI::VrText(FString::Printf(TEXT("%s：%s"), *Spec.Name, *Spec.Tip)));
	}
	return FText::FromString(TEXT("每次只改一两个参数，跑完对比结果，最容易看出它的作用。"));
}

bool AToyEtcherRig::IsPresetSelected(int32 Index) const
{
	if (!Content.Presets.IsValidIndex(Index))
	{
		return false;
	}
	const ToyEtch::FRecipe& A = Process.R;
	const ToyEtch::FRecipe& B = Content.Presets[Index].R;
	return A.P == B.P && A.Ws == B.Ws && A.Wb == B.Wb && A.Cl2 == B.Cl2 && A.HBr == B.HBr && A.O2 == B.O2 && A.T == B.T && A.Oe == B.Oe;
}

FText AToyEtcherRig::PresetName(int32 Index) const
{
	return Content.Presets.IsValidIndex(Index) ? FText::FromString(ToyEtchUI::VrText(Content.Presets[Index].Name)) : FText();
}

FText AToyEtcherRig::PredictText() const
{
	// 网页版 syncFields() 里的"预测"列表（这几行字在 app.js 里，不在共用的 etch_core.js 里）
	const ToyEtch::FModel& M = Process.M;
	if (M.Problem != ToyEtch::EProblem::None)
	{
		const FString* Problem = Content.Problems.Find(UTF8_TO_TCHAR(ToyEtch::ProblemKey(M.Problem)));
		return FText::FromString(FString::Printf(TEXT("问题\n%s"), Problem ? **Problem : TEXT("点不着等离子体")));
	}
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("等离子体密度（相对）  %.2f"), M.n));
	Lines.Add(FString::Printf(TEXT("离子能量  约 %.0f eV"), M.Ei));
	Lines.Add(FString::Printf(TEXT("多晶硅往下刻  %.0f nm/min"), M.ERv));
	Lines.Add(FString::Printf(TEXT("往侧面刻  %.1f nm/min"), M.ERl));
	Lines.Add(FString::Printf(TEXT("对栅氧选择比  %.0f : 1"), M.sel));
	Lines.Add(FString::Printf(TEXT("光刻胶消耗  %.0f nm/min"), M.ERmask));
	Lines.Add(M.ERv > 0.5 ? FString::Printf(TEXT("预计主刻时间  %.0f s"), 100 / M.ERv * 60) : FString(TEXT("预计主刻时间  刻不动")));
	Lines.Add(FString::Printf(TEXT("中心/边缘速率差  ±%.1f%%"), M.u * 100));
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FText AToyEtcherRig::VerdictText() const
{
	if (!Process.bHasResult)
	{
		return FText::FromString(TEXT("还没有结果：跑完一片才有"));
	}
	int32 Bad = 0;
	for (const bool bOk : Process.Result.V.Ok)
	{
		Bad += bOk ? 0 : 1;
	}
	if (Bad == 0)
	{
		return FText::FromString(ToyEtchUI::VrText(Content.VerdictOk.IsEmpty() ? TEXT("合格：这片晶圆的栅极刻得很好") : Content.VerdictOk));
	}
	const FString Pattern = Content.VerdictBad.IsEmpty() ? TEXT("有 {n} 项不合格") : Content.VerdictBad;
	TMap<FString, FString> Values;
	Values.Add(TEXT("n"), FString::FromInt(Bad));
	return FText::FromString(FToyEtchContent::Fill(Pattern, Values));
}

bool AToyEtcherRig::IsVerdictOk() const
{
	const ToyEtch::FEvaluation& V = Process.Result.V;
	return Process.bHasResult && V.Ok[0] && V.Ok[1] && V.Ok[2] && V.Ok[3] && V.Ok[4];
}

FText AToyEtcherRig::ResultLabel(int32 Index) const
{
	return Content.Results.IsValidIndex(Index) ? FText::FromString(ToyEtchUI::VrText(Content.Results[Index].Label)) : FText();
}

FText AToyEtcherRig::ResultValue(int32 Index) const
{
	if (!Process.bHasResult)
	{
		return FText::FromString(TEXT("—"));
	}
	// 网页版 finish() 的 vals
	const ToyEtch::FEtch& E = Process.Result.E;
	const ToyEtch::FEvaluation& V = Process.Result.V;
	switch (Index)
	{
	case 0: return FText::FromString(E.bTimeout ? TEXT("没刻完") : V.bResidue ? FString::Printf(TEXT("边缘还剩 %.0f nm"), 100 - E.De) : FString(TEXT("干净")));
	case 1: return FText::FromString(FString::Printf(TEXT("%.1f nm"), V.SideCutMax));
	case 2: return FText::FromString(E.Si > 0 ? FString::Printf(TEXT("刻穿了，硅被挖下 %.1f nm"), E.Si) : FString::Printf(TEXT("剩 %.1f / 4 nm"), E.Ox));
	case 3: return FText::FromString(FString::Printf(TEXT("剩 %.0f / 60 nm"), E.MaskH));
	case 4: return FText::FromString(FString::Printf(TEXT("%.0f 秒"), E.tMain));
	default: return FText();
	}
}

bool AToyEtcherRig::IsResultOk(int32 Index) const
{
	return Process.bHasResult && Index >= 0 && Index < 5 && Process.Result.V.Ok[Index];
}

FText AToyEtcherRig::ResultHint(int32 Index) const
{
	if (!Process.bHasResult || IsResultOk(Index) || !Content.Results.IsValidIndex(Index))
	{
		return FText();
	}
	const FToyEtchResultText& Text = Content.Results[Index];
	const bool bTimeout = Process.Result.E.bTimeout && !Text.HintTimeout.IsEmpty();
	return FText::FromString(ToyEtchUI::VrText(bTimeout ? Text.HintTimeout : Text.Hint));
}

FText AToyEtcherRig::ResultStats() const
{
	if (!Process.bHasResult)
	{
		return FText();
	}
	const ToyEtch::FRunResult& Res = Process.Result;
	const FString Endpoint = Res.E.bTimeout ? FString(TEXT("未到")) : FString::Printf(TEXT("%.1f s"), Res.E.tEp);
	const double Over = Res.E.bTimeout ? 0.0 : Res.E.tMain * Res.R.Oe / 100;
	return FText::FromString(FString::Printf(TEXT("多晶硅刻蚀速率 %.0f nm/min · 离子能量约 %.0f eV · 对栅氧选择比 %.0f : 1 · 终点 %s · 过刻 %.1f s"),
		Res.M.ERv, Res.M.Ei, Res.M.sel, *Endpoint, Over));
}
