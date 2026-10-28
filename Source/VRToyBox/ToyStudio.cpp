#include "ToyStudio.h"

#include "ToyProp.h"
#include "ToyExplodedModel.h"
#include "ToyMenuPanel.h"
#include "ToyShapeComponent.h"
#include "ToyTouchScreen.h"
#include "Components/ChildActorComponent.h"
#include "Components/PointLightComponent.h"
#include "Algo/AnyOf.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace
{
	// 尺寸单位厘米
	constexpr float RoomHalf = 300.f;
	constexpr float RoomHeight = 300.f;
	constexpr float WallThickness = 10.f;

	// 桌子：近边离玩家 40cm，伸手就够得着；桌面高 75cm
	constexpr float TableX = 75.f;
	constexpr float TableDepth = 70.f;
	constexpr float TableWidth = 160.f;
	constexpr float TableHeight = 75.f;
	constexpr float TableTopThickness = 4.f;

	const FLinearColor FloorColor(0.35f, 0.22f, 0.12f);
	const FLinearColor WallColor(0.82f, 0.80f, 0.76f);
	const FLinearColor AccentWallColor(0.22f, 0.42f, 0.48f);
	const FLinearColor CeilingColor(0.9f, 0.9f, 0.9f);
	const FLinearColor RugColor(0.45f, 0.12f, 0.12f);
	const FLinearColor TableColor(0.62f, 0.43f, 0.25f);
	const FLinearColor LegColor(0.1f, 0.1f, 0.1f);
	const FLinearColor ShelfColor(0.3f, 0.2f, 0.12f);

	// 调试命令 toy.Screen <按钮名>：和戳触屏上那个按钮一样（Props / Motor / Valve / Drone / Lathe / Meter / Server / Robot / Engine / Hand / Etcher / Hide / Explode / Reset /
	// MoveNear / MoveFar / MoveLeft / MoveRight），可以在 -ExecCmds 里连着写，不用鼠标点
	void ScreenCommand(const TArray<FString>& Args, UWorld* World)
	{
		static const TMap<FString, EToyScreenAction> Names = {
			{ TEXT("props"), EToyScreenAction::ResetProps }, { TEXT("resetprops"), EToyScreenAction::ResetProps },
			{ TEXT("motor"), EToyScreenAction::ToggleMotor }, { TEXT("valve"), EToyScreenAction::ToggleValve },
			{ TEXT("drone"), EToyScreenAction::ToggleDrone }, { TEXT("lathe"), EToyScreenAction::ToggleLathe },
			{ TEXT("meter"), EToyScreenAction::ToggleMultimeter }, { TEXT("multimeter"), EToyScreenAction::ToggleMultimeter },
			{ TEXT("server"), EToyScreenAction::ToggleServer }, { TEXT("aiserver"), EToyScreenAction::ToggleServer },
			{ TEXT("robot"), EToyScreenAction::ToggleOrb }, { TEXT("orb"), EToyScreenAction::ToggleOrb },
			{ TEXT("engine"), EToyScreenAction::ToggleEngine },
			{ TEXT("hand"), EToyScreenAction::ToggleDexHand }, { TEXT("dexhand"), EToyScreenAction::ToggleDexHand },
			{ TEXT("etcher"), EToyScreenAction::ToggleEtcher }, { TEXT("etch"), EToyScreenAction::ToggleEtcher },
			{ TEXT("explode"), EToyScreenAction::ToggleExplode }, { TEXT("reset"), EToyScreenAction::ResetParts },
			{ TEXT("movenear"), EToyScreenAction::MoveNear }, { TEXT("movefar"), EToyScreenAction::MoveFar },
			{ TEXT("moveleft"), EToyScreenAction::MoveLeft }, { TEXT("moveright"), EToyScreenAction::MoveRight },
			{ TEXT("menu"), EToyScreenAction::OpenMenu }, { TEXT("hide"), EToyScreenAction::HideModel },
		};
		AToyStudio* Studio = nullptr;
		for (TActorIterator<AToyStudio> It(World); It && !Studio; ++It)
		{
			Studio = *It;
		}
		for (const FString& Arg : Args)
		{
			const EToyScreenAction* Action = Names.Find(Arg.ToLower());
			if (!Studio || !Action)
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Screen 不认识 %s（或者没有工作室）"), *Arg);
				continue;
			}
			Studio->HandleScreenAction(*Action);
			UE_LOG(LogTemp, Log, TEXT("ToyBox: toy.Screen %s → %s"), *Arg, *Studio->GetStatusText());
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ScreenCommandRef(TEXT("toy.Screen"),
		TEXT("和戳触屏按钮一样：toy.Screen Lathe MoveNear MoveNear Explode（可以连着写几个）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ScreenCommand));

	FAutoConsoleCommandWithWorldAndArgs UiBrightnessCommand(TEXT("toy.UiBrightness"),
		TEXT("现场调 UiBrightnessScale（触屏马上变；零件信息牌下次开局才跟着变）：toy.UiBrightness 3。调好了填到关卡里 ToyStudio 上"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AToyStudio* Studio = AToyStudio::Find(World);
			if (!Studio || Args.Num() == 0)
			{
				return;
			}
			Studio->UiBrightnessScale = FCString::Atof(*Args[0]);
			for (TActorIterator<AToyTouchScreen> It(World); It; ++It)
			{
				It->SetStudio(Studio);
			}
			UE_LOG(LogTemp, Log, TEXT("ToyBox: UiBrightnessScale = %.2f"), Studio->UiBrightnessScale);
		}));
}

AToyStudio::AToyStudio()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	auto Box = [this](FName Name, const FVector& Size, const FVector& Location, const FLinearColor& Color)
	{
		return UToyShapeComponent::Create(this, Name, Root, EToyShape::Box, Size, Location, Color, FRotator::ZeroRotator);
	};

	// 房间：地板上表面在 Z = 0
	Box(TEXT("Floor"), FVector(RoomHalf * 2.f, RoomHalf * 2.f, WallThickness), FVector(0.f, 0.f, -WallThickness * 0.5f), FloorColor);
	Box(TEXT("Ceiling"), FVector(RoomHalf * 2.f, RoomHalf * 2.f, WallThickness), FVector(0.f, 0.f, RoomHeight + WallThickness * 0.5f), CeilingColor);
	Box(TEXT("WallFront"), FVector(WallThickness, RoomHalf * 2.f, RoomHeight), FVector(RoomHalf + WallThickness * 0.5f, 0.f, RoomHeight * 0.5f), AccentWallColor);
	Box(TEXT("WallBack"), FVector(WallThickness, RoomHalf * 2.f, RoomHeight), FVector(-RoomHalf - WallThickness * 0.5f, 0.f, RoomHeight * 0.5f), WallColor);
	Box(TEXT("WallLeft"), FVector(RoomHalf * 2.f, WallThickness, RoomHeight), FVector(0.f, RoomHalf + WallThickness * 0.5f, RoomHeight * 0.5f), WallColor);
	Box(TEXT("WallRight"), FVector(RoomHalf * 2.f, WallThickness, RoomHeight), FVector(0.f, -RoomHalf - WallThickness * 0.5f, RoomHeight * 0.5f), WallColor);
	Box(TEXT("Rug"), FVector(220.f, 200.f, 1.f), FVector(TableX - 20.f, 0.f, 0.5f), RugColor);

	// 正对面墙上的置物架，纯装饰，给空间一点"工作室"的感觉
	Box(TEXT("Shelf"), FVector(25.f, 160.f, 3.f), FVector(RoomHalf - 12.5f, 0.f, 140.f), ShelfColor);
	Box(TEXT("ShelfToyA"), FVector(18.f, 18.f, 18.f), FVector(RoomHalf - 14.f, -50.f, 150.5f), FLinearColor(0.9f, 0.75f, 0.1f));
	Box(TEXT("ShelfToyB"), FVector(14.f, 30.f, 10.f), FVector(RoomHalf - 14.f, 10.f, 146.5f), FLinearColor(0.2f, 0.5f, 0.9f));
	UToyShapeComponent::Create(this, TEXT("ShelfToyC"), Root, EToyShape::Sphere, FVector(16.f), FVector(RoomHalf - 14.f, 55.f, 149.5f),
		FLinearColor(0.9f, 0.3f, 0.5f), FRotator::ZeroRotator);

	// 桌子
	Box(TEXT("TableTop"), FVector(TableDepth, TableWidth, TableTopThickness),
		FVector(TableX, 0.f, TableHeight - TableTopThickness * 0.5f), TableColor);
	const float LegHeight = TableHeight - TableTopThickness;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float LegX = TableX + ((Index & 1) ? 1.f : -1.f) * (TableDepth * 0.5f - 6.f);
		const float LegY = ((Index & 2) ? 1.f : -1.f) * (TableWidth * 0.5f - 6.f);
		UToyShapeComponent::Create(this, *FString::Printf(TEXT("TableLeg%d"), Index), Root, EToyShape::Cylinder,
			FVector(5.f, 5.f, LegHeight), FVector(LegX, LegY, LegHeight * 0.5f), LegColor, FRotator::ZeroRotator);
	}

	// 灯：全部可移动光源，不需要烘焙。三盏都投影：主灯给清楚的影子，补光和背光给淡一点的第二、三道影子，看起来更立体
	auto Light = [this](FName Name, const FVector& Location, float Lumens, bool bShadows)
	{
		UPointLightComponent* PointLight = CreateDefaultSubobject<UPointLightComponent>(Name);
		PointLight->SetupAttachment(Root);
		PointLight->SetRelativeLocation(Location);
		PointLight->SetMobility(EComponentMobility::Movable);
		PointLight->IntensityUnits = ELightUnits::Lumens;
		PointLight->Intensity = Lumens;
		PointLight->AttenuationRadius = 900.f;
		PointLight->SetCastShadows(bShadows);
		return PointLight;
	};
	Light(TEXT("MainLight"), FVector(TableX, 0.f, 250.f), 6000.f, true);
	Light(TEXT("FillLight"), FVector(-150.f, 150.f, 230.f), 2500.f, true);
	Light(TEXT("BackLight"), FVector(200.f, -180.f, 220.f), 2000.f, true);
	UToyShapeComponent* Bulb = UToyShapeComponent::Create(this, TEXT("LampShade"), Root, EToyShape::Cylinder,
		FVector(40.f, 40.f, 4.f), FVector(TableX, 0.f, RoomHeight - 2.f), FLinearColor(1.f, 0.95f, 0.85f), FRotator::ZeroRotator);
	Bulb->SetCastShadow(false);


	// 所有模型的支架都挂在 ModelAnchor 下面：触屏的 NEAR / FAR / LEFT / RIGHT 挪的就是它，换模型也保持挪过的位置。
	// 默认放在离站立点 70cm（桌子中间偏近），以前放在 90cm，伸手够不着
	ModelAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("ModelAnchor"));
	ModelAnchor->SetupAttachment(Root);
	ModelAnchor->SetRelativeLocation(ModelHome);

	// 电机：Yaw -90 让电机轴（模型 X）沿桌子长边，轴伸端朝左手边，
	// 爆炸时轴伸端那侧（转子、前端盖）飞得最远，往左边飞就不会撞到右边的触屏
	MotorMount = CreateDefaultSubobject<USceneComponent>(TEXT("MotorMount"));
	MotorMount->SetupAttachment(ModelAnchor);
	MotorMount->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// 球阀：同一个位置，Yaw 90。球阀模型的 X（管道方向 = 执行器缸体长轴）转到桌子长边上，端盖、活塞往左右两边飞，
	// 不会朝玩家脸上飞；电磁阀（模型 +Y）转到朝着玩家的一面
	ValveMount = CreateDefaultSubobject<USceneComponent>(TEXT("ValveMount"));
	ValveMount->SetupAttachment(ModelAnchor);
	ValveMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 无人机：同一个位置。模型 +Y 是机头（相机那边），Yaw 90 让机头朝向玩家（-X）
	DroneMount = CreateDefaultSubobject<USceneComponent>(TEXT("DroneMount"));
	DroneMount->SetupAttachment(ModelAnchor);
	DroneMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 迷你车床：同一个位置。模型 X 是床身方向（+X 主轴箱），+Y 是正面（电机、手轮把手、刻度手轮）。
	// Yaw 90 让床身沿桌子长边、正面朝向玩家（-X），主轴箱在右手边；皮带罩往右飞，不碰到左边
	LatheMount = CreateDefaultSubobject<USceneComponent>(TEXT("LatheMount"));
	LatheMount->SetupAttachment(ModelAnchor);
	LatheMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 万用表：同一个位置，面朝上平放。模型 -Y 是屏幕那头（Blender 里的 +Y），+X 是红表笔那边。
	// Yaw 90 让屏幕那头朝远处、插孔和表笔插头朝着玩家，红表笔在右手边；拆开后屏幕叠层在左、面壳在右
	MultimeterMount = CreateDefaultSubobject<USceneComponent>(TEXT("MultimeterMount"));
	MultimeterMount->SetupAttachment(ModelAnchor);
	MultimeterMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// AI 服务器：同一个位置。模型 +Y 是前面板（Blender 里的 -Y：风扇、硬盘），+X 是 Blender 的 +X。
	// Yaw 180 让前面板朝左手边、后面（电源、网卡口）朝右手边、Blender +X 那个侧面朝着玩家。
	// 爆炸时 GPU 托盘往左上方抽出，主板托盘往右抽出后再往远处让 13cm，不碰右手边的触屏
	ServerMount = CreateDefaultSubobject<USceneComponent>(TEXT("ServerMount"));
	ServerMount->SetupAttachment(ModelAnchor);
	ServerMount->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

	// 球形漂浮机器人：同一个位置。模型 +Y 是主眼（Blender 里的 -Y），Yaw 90 让主眼朝向玩家（-X）。
	// 爆炸时涡轮发电机（模型 +X）往右手边拉出、电子舱往左手边，燃料舱和风扇往上，镜片沿视线朝玩家排开约 20cm
	OrbMount = CreateDefaultSubobject<USceneComponent>(TEXT("OrbMount"));
	OrbMount->SetupAttachment(ModelAnchor);
	OrbMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 四缸发动机：同一个位置往远处挪 6cm。模型 +Y 是进气侧（Blender 里的 -Y），Yaw 90 让进气歧管朝向玩家（-X），
	// 正时端（模型 -X）转到左手边。爆炸时附件和正时往左、飞轮和排气往右、缸盖往后，下半截像抽屉一样往玩家这边拉出
	EngineMount = CreateDefaultSubobject<USceneComponent>(TEXT("EngineMount"));
	EngineMount->SetupAttachment(ModelAnchor);
	EngineMount->SetRelativeLocationAndRotation(FVector(6.f, 0.f, 0.f), FRotator(0.f, 90.f, 0.f));

	// 五指灵巧手：法兰朝下立着，模型 +Y 是手心，Yaw 90 让手心朝向玩家（-X），拇指（模型 +X）转到玩家右手边
	DexHandMount = CreateDefaultSubobject<USceneComponent>(TEXT("DexHandMount"));
	DexHandMount->SetupAttachment(ModelAnchor);
	DexHandMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 干法刻蚀机：模型 +Y 是闸阀（晶圆进出的门），Yaw 90 让闸阀朝向玩家（-X），气体盒（模型 +X）转到玩家右手边
	EtcherMount = CreateDefaultSubobject<USceneComponent>(TEXT("EtcherMount"));
	EtcherMount->SetupAttachment(ModelAnchor);
	EtcherMount->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// 触屏：桌面右手边靠前，转过来正对玩家站立点
	const FVector ScreenLocation(TableX - 20.f, 60.f, TableHeight);
	ScreenMount = CreateDefaultSubobject<USceneComponent>(TEXT("ScreenMount"));
	ScreenMount->SetupAttachment(Root);
	ScreenMount->SetRelativeLocationAndRotation(ScreenLocation, FVector(-ScreenLocation.X, -ScreenLocation.Y, 0.f).Rotation());
	ScreenChild = CreateDefaultSubobject<UChildActorComponent>(TEXT("ScreenChild"));
	ScreenChild->SetupAttachment(ScreenMount);
	ScreenChild->SetChildActorClass(AToyTouchScreen::StaticClass());
}

AToyStudio* AToyStudio::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AToyStudio> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AToyStudio::IsFloorHit(const FHitResult& Hit) const
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Component || Hit.ImpactNormal.Z < 0.8f)
	{
		return false;
	}
	if (bBuildRoom)
	{
		const FString Name = Component->GetName();
		return Hit.GetActor() == this && (Name.StartsWith(TEXT("Floor")) || Name.StartsWith(TEXT("Rug")));
	}
	// 套用别的房间：样板间的地板、地毯都行；沙发座面、桌面这些比站立点高出一截的不算
	const FVector Local = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
	return Hit.GetActor() != this && FMath::Abs(Local.Z) < 5.f
		&& Local.X >= WalkArea.Min.X && Local.X <= WalkArea.Max.X && Local.Y >= WalkArea.Min.Y && Local.Y <= WalkArea.Max.Y;
}

void AToyStudio::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyRoomVisibility();
}

void AToyStudio::ApplyRoomVisibility()
{
	// 自己的房间：构造函数里按名字建的那些组件。桌子、触屏、模型支架不在里面；主灯（桌子正上方）留着，只改照射半径
	static const TCHAR* const RoomPrefixes[] = { TEXT("MainLight"), TEXT("Floor"), TEXT("Ceiling"), TEXT("Wall"), TEXT("Rug"), TEXT("Shelf"),
		TEXT("LampShade"), TEXT("FillLight"), TEXT("BackLight") };
	TInlineComponentArray<USceneComponent*> Components(this);
	for (USceneComponent* Component : Components)
	{
		const FString Name = Component->GetName();
		const bool bRoomPart = Algo::AnyOf(RoomPrefixes, [&Name](const TCHAR* Prefix) { return Name.StartsWith(Prefix); });
		if (!bRoomPart)
		{
			continue;
		}
		if (UPointLightComponent* Light = Cast<UPointLightComponent>(Component); Light && Name == TEXT("MainLight"))
		{
			Light->SetAttenuationRadius(bBuildRoom ? 900.f : DeskLightRadiusCm);
			continue;
		}
		Component->SetVisibility(bBuildRoom);
		Component->SetHiddenInGame(!bBuildRoom);
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->SetCollisionEnabled(bBuildRoom ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
	}
}

void AToyStudio::BeginPlay()
{
	Super::BeginPlay();
	ApplyRoomVisibility();

	// 关卡里能拿起来的摆件（样板间生成脚本放的），触屏「摆件复位」用
	for (TActorIterator<AToyProp> It(GetWorld()); It; ++It)
	{
		Props.Add(*It);
	}

	// L_Studio 是在有 ModelAnchor 之前存的：关卡里的工作室把电机 / 阀门 / 无人机的支架存成挂在 Root 上（原点 = 地板），
	// 构造函数里的 SetupAttachment 盖不过关卡里存的值。这里统一挂回 ModelAnchor，不然这三个模型摆在桌子前面的地上
	for (USceneComponent* Mount : { MotorMount.Get(), ValveMount.Get(), DroneMount.Get(), LatheMount.Get(), MultimeterMount.Get(), ServerMount.Get(), OrbMount.Get(), EngineMount.Get(), DexHandMount.Get(), EtcherMount.Get() })
	{
		if (Mount && Mount->GetAttachParent() != ModelAnchor)
		{
			Mount->AttachToComponent(ModelAnchor, FAttachmentTransformRules::KeepRelativeTransform);
			Mount->SetRelativeLocation(FVector::ZeroVector);
		}
	}

	// 触屏是构造时建好的子 Actor（编辑器里也看得见），这里只接上回调
	Screen = Cast<AToyTouchScreen>(ScreenChild->GetChildActor());
	if (Screen)
	{
		Screen->SetStudio(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ToyBox: 工作室的触屏子 Actor 没生成"));
	}

	// 主菜单：进游戏先看到 START / SETTINGS。-ExecCmds 里的调试命令（toy.GrabAudit 等）想直接开始，用 toy.Menu close
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Menu = GetWorld()->SpawnActor<AToyMenuPanel>(AToyMenuPanel::StaticClass(), GetActorTransform(), Params);
	if (Menu)
	{
		Menu->Open();
	}
}

void AToyStudio::ResetProps()
{
	for (const TWeakObjectPtr<AToyProp>& Prop : Props)
	{
		if (AToyProp* Alive = Prop.Get())
		{
			Alive->ResetToHome();
		}
	}
}

FString AToyStudio::KindName(EToyModelKind Kind)
{
	switch (Kind)
	{
	case EToyModelKind::Motor: return TEXT("MOTOR");
	case EToyModelKind::Valve: return TEXT("VALVE");
	case EToyModelKind::Lathe: return TEXT("LATHE");
	case EToyModelKind::Multimeter: return TEXT("METER");
	case EToyModelKind::Server:     return TEXT("SERVER");
	case EToyModelKind::Orb:        return TEXT("ROBOT");
	case EToyModelKind::Engine:     return TEXT("ENGINE");
	case EToyModelKind::DexHand:    return TEXT("HAND");
	case EToyModelKind::Etcher:     return TEXT("ETCHER");
	default:                   return TEXT("DRONE");
	}
}

void AToyStudio::HandleScreenAction(EToyScreenAction Action)
{
	switch (Action)
	{
	case EToyScreenAction::ResetProps:    ResetProps(); break;
	case EToyScreenAction::ToggleMotor:   ToggleModel(EToyModelKind::Motor); break;
	case EToyScreenAction::ToggleValve:   ToggleModel(EToyModelKind::Valve); break;
	case EToyScreenAction::ToggleDrone:   ToggleModel(EToyModelKind::Drone); break;
	case EToyScreenAction::ToggleLathe:   ToggleModel(EToyModelKind::Lathe); break;
	case EToyScreenAction::ToggleMultimeter: ToggleModel(EToyModelKind::Multimeter); break;
	case EToyScreenAction::ToggleServer:  ToggleModel(EToyModelKind::Server); break;
	case EToyScreenAction::ToggleOrb:     ToggleModel(EToyModelKind::Orb); break;
	case EToyScreenAction::ToggleEngine:  ToggleModel(EToyModelKind::Engine); break;
	case EToyScreenAction::ToggleDexHand: ToggleModel(EToyModelKind::DexHand); break;
	case EToyScreenAction::ToggleEtcher:  ToggleModel(EToyModelKind::Etcher); break;
	case EToyScreenAction::ToggleExplode: ToggleModelExploded(); break;
	case EToyScreenAction::ResetParts:    ResetModel(); break;
	case EToyScreenAction::MoveNear:      MoveModel(FVector(-MoveStepCm, 0.f, 0.f)); break;
	case EToyScreenAction::MoveFar:       MoveModel(FVector(MoveStepCm, 0.f, 0.f)); break;
	case EToyScreenAction::MoveLeft:      MoveModel(FVector(0.f, -MoveStepCm, 0.f)); break;
	case EToyScreenAction::MoveRight:     MoveModel(FVector(0.f, MoveStepCm, 0.f)); break;
	case EToyScreenAction::OpenMenu:      if (Menu) { Menu->Open(); } break;
	case EToyScreenAction::HideModel:     if (Model) { ToggleModel(ModelKind); } break;
	// 翻页只是触屏自己的事，不会传到这里
	case EToyScreenAction::PrevPage:
	case EToyScreenAction::NextPage:      break;
	}
	// 触屏界面每帧从工作室读状态，不用在这里刷新
}

void AToyStudio::MoveModel(const FVector& DeltaCm)
{
	// 挪的是所有模型共用的 ModelAnchor（玩家面朝 +X，所以 -X = 拉近、+Y = 往右）。
	// 限制在桌面范围里：离站立点 55~95cm（桌子前沿在 40cm，留出模型自己的半个深度），
	// 左右 -40~+20cm（右边是触屏，再往右会撞上）
	const FVector Next = ModelAnchor->GetRelativeLocation() + DeltaCm;
	const FVector Clamped(FMath::Clamp(Next.X, TableX - 20.f, TableX + 20.f), FMath::Clamp(Next.Y, -40.f, 20.f), ModelHome.Z);
	ModelAnchor->SetRelativeLocation(Clamped);
}

float AToyStudio::GetModelDistanceCm() const
{
	return static_cast<float>(ModelAnchor->GetRelativeLocation().X);
}

bool AToyStudio::ToggleModel(EToyModelKind Kind)
{
	// 再按一次同一个按钮 = 收起；按另一个模型的按钮 = 换模型
	const bool bSame = Model && ModelKind == Kind;
	if (Model)
	{
		Model->Destroy();
		Model = nullptr;
	}
	if (bSame)
	{
		return true;
	}

	USceneComponent* Mount = DroneMount;
	const FString* Manifest = &DroneManifest;
	switch (Kind)
	{
	case EToyModelKind::Motor: Mount = MotorMount; Manifest = &MotorManifest; break;
	case EToyModelKind::Valve: Mount = ValveMount; Manifest = &ValveManifest; break;
	case EToyModelKind::Lathe: Mount = LatheMount; Manifest = &LatheManifest; break;
	case EToyModelKind::Multimeter: Mount = MultimeterMount; Manifest = &MultimeterManifest; break;
	case EToyModelKind::Server: Mount = ServerMount; Manifest = &ServerManifest; break;
	case EToyModelKind::Orb: Mount = OrbMount; Manifest = &OrbManifest; break;
	case EToyModelKind::Engine: Mount = EngineMount; Manifest = &EngineManifest; break;
	case EToyModelKind::DexHand: Mount = DexHandMount; Manifest = &DexHandManifest; break;
	case EToyModelKind::Etcher: Mount = EtcherMount; Manifest = &EtcherManifest; break;
	default: break;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToyExplodedModel* Spawned = GetWorld()->SpawnActor<AToyExplodedModel>(AToyExplodedModel::StaticClass(),
		Mount->GetComponentTransform(), Params);
	if (!Spawned)
	{
		return false;
	}
	Spawned->TargetLengthCm = ModelLengthCm;
	if (!Spawned->LoadManifest(FPaths::ProjectContentDir() / *Manifest))
	{
		Spawned->Destroy();
		MissingKind = Kind;
		return false;
	}
	Spawned->AttachToComponent(Mount, FAttachmentTransformRules::KeepWorldTransform);
	Model = Spawned;
	ModelKind = Kind;
	MissingKind.Reset();
	return true;
}

void AToyStudio::ToggleModelExploded()
{
	// 桌上还没有模型时，按"爆炸"就先摆出电机再炸开，省得多按一次
	if (!Model && !ToggleModel(EToyModelKind::Motor))
	{
		return;
	}
	Model->SetExploded(!Model->IsExploded());
}

void AToyStudio::ResetModel()
{
	if (Model)
	{
		Model->ResetAll();
	}
}

bool AToyStudio::IsModelShown(EToyModelKind Kind) const
{
	return Model != nullptr && ModelKind == Kind;
}

bool AToyStudio::IsModelExploded() const
{
	return Model && Model->IsExploded();
}

FString AToyStudio::GetStatusText() const
{
	if (MissingKind)
	{
		return FString::Printf(TEXT("%s NOT IMPORTED"), *KindName(*MissingKind));
	}
	const FString Name = !Model ? TEXT("MODEL") : KindName(ModelKind);
	const FString State = !Model ? TEXT("OFF") : (Model->IsExploded() ? TEXT("EXPLODED") : TEXT("ASSEMBLED"));
	return FString::Printf(TEXT("PROPS %d AWAY / %d    %s %s    %.0f CM AWAY"), GetPropsAwayCount(), GetPropCount(), *Name, *State,
		GetModelDistanceCm());
}

int32 AToyStudio::GetPropCount() const
{
	return Props.FilterByPredicate([](const TWeakObjectPtr<AToyProp>& Prop) { return Prop.IsValid(); }).Num();
}

int32 AToyStudio::GetPropsAwayCount() const
{
	return Props.FilterByPredicate([](const TWeakObjectPtr<AToyProp>& Prop) { return Prop.IsValid() && Prop->IsAwayFromHome(); }).Num();
}
