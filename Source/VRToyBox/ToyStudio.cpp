#include "ToyStudio.h"

#include "ToyCube.h"
#include "ToyExplodedModel.h"
#include "ToyMenuPanel.h"
#include "ToyShapeComponent.h"
#include "ToyTouchScreen.h"
#include "Components/ChildActorComponent.h"
#include "Components/PointLightComponent.h"
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

	// 调试命令 toy.Screen <按钮名>：和戳触屏上那个按钮一样（Spawn / Clear / Motor / Valve / Drone / Lathe / Explode / Reset /
	// MoveNear / MoveFar / MoveLeft / MoveRight），可以在 -ExecCmds 里连着写，不用鼠标点
	void ScreenCommand(const TArray<FString>& Args, UWorld* World)
	{
		static const TMap<FString, EToyScreenAction> Names = {
			{ TEXT("spawn"), EToyScreenAction::SpawnCube }, { TEXT("clear"), EToyScreenAction::ClearCubes },
			{ TEXT("motor"), EToyScreenAction::ToggleMotor }, { TEXT("valve"), EToyScreenAction::ToggleValve },
			{ TEXT("drone"), EToyScreenAction::ToggleDrone }, { TEXT("lathe"), EToyScreenAction::ToggleLathe },
			{ TEXT("explode"), EToyScreenAction::ToggleExplode }, { TEXT("reset"), EToyScreenAction::ResetParts },
			{ TEXT("movenear"), EToyScreenAction::MoveNear }, { TEXT("movefar"), EToyScreenAction::MoveFar },
			{ TEXT("moveleft"), EToyScreenAction::MoveLeft }, { TEXT("moveright"), EToyScreenAction::MoveRight },
			{ TEXT("menu"), EToyScreenAction::OpenMenu },
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

	// 灯：全部可移动光源，不需要烘焙。只有主灯投影，省性能
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
	Light(TEXT("FillLight"), FVector(-150.f, 150.f, 230.f), 2500.f, false);
	Light(TEXT("BackLight"), FVector(200.f, -180.f, 220.f), 2000.f, false);
	UToyShapeComponent* Bulb = UToyShapeComponent::Create(this, TEXT("LampShade"), Root, EToyShape::Cylinder,
		FVector(40.f, 40.f, 4.f), FVector(TableX, 0.f, RoomHeight - 2.f), FLinearColor(1.f, 0.95f, 0.85f), FRotator::ZeroRotator);
	Bulb->SetCastShadow(false);

	// 方块出生点：桌面左手边靠前的角落（UE 里 +Y 是右边），离桌面 15cm 落下（桌面中间留给电机）
	SpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	SpawnPoint->SetRelativeLocation(FVector(TableX - 20.f, -60.f, TableHeight + 15.f));

	// 四个模型的支架都挂在 ModelAnchor 下面：触屏的 NEAR / FAR / LEFT / RIGHT 挪的就是它，换模型也保持挪过的位置。
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

	// 触屏：桌面右手边靠前，转过来正对玩家站立点
	const FVector ScreenLocation(TableX - 20.f, 60.f, TableHeight);
	ScreenMount = CreateDefaultSubobject<USceneComponent>(TEXT("ScreenMount"));
	ScreenMount->SetupAttachment(Root);
	ScreenMount->SetRelativeLocationAndRotation(ScreenLocation, FVector(-ScreenLocation.X, -ScreenLocation.Y, 0.f).Rotation());
	ScreenChild = CreateDefaultSubobject<UChildActorComponent>(TEXT("ScreenChild"));
	ScreenChild->SetupAttachment(ScreenMount);
	ScreenChild->SetChildActorClass(AToyTouchScreen::StaticClass());
}

void AToyStudio::BeginPlay()
{
	Super::BeginPlay();

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

bool AToyStudio::SpawnCube()
{
	Cubes.RemoveAll([](const TWeakObjectPtr<AToyCube>& Cube) { return !Cube.IsValid(); });
	if (Cubes.Num() >= MaxCubes)
	{
		return false;
	}

	const FVector Offset(FMath::FRandRange(-8.f, 8.f), FMath::FRandRange(-8.f, 8.f), 0.f);
	const FTransform SpawnTransform(FRotator(0.f, FMath::FRandRange(0.f, 90.f), 0.f), SpawnPoint->GetComponentLocation() + Offset);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AToyCube* Cube = GetWorld()->SpawnActor<AToyCube>(AToyCube::StaticClass(), SpawnTransform, Params);
	if (!Cube)
	{
		return false;
	}
	Cube->SetBaseColor(FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::RandRange(0, 255)), 190, 235));
	Cubes.Add(Cube);
	return true;
}

void AToyStudio::ClearCubes()
{
	for (const TWeakObjectPtr<AToyCube>& Cube : Cubes)
	{
		if (AToyCube* Alive = Cube.Get())
		{
			Alive->Destroy();
		}
	}
	Cubes.Reset();
}

FString AToyStudio::KindName(EToyModelKind Kind)
{
	switch (Kind)
	{
	case EToyModelKind::Motor: return TEXT("MOTOR");
	case EToyModelKind::Valve: return TEXT("VALVE");
	case EToyModelKind::Lathe: return TEXT("LATHE");
	default:                   return TEXT("DRONE");
	}
}

void AToyStudio::HandleScreenAction(EToyScreenAction Action)
{
	switch (Action)
	{
	case EToyScreenAction::SpawnCube:     SpawnCube(); break;
	case EToyScreenAction::ClearCubes:    ClearCubes(); break;
	case EToyScreenAction::ToggleMotor:   ToggleModel(EToyModelKind::Motor); break;
	case EToyScreenAction::ToggleValve:   ToggleModel(EToyModelKind::Valve); break;
	case EToyScreenAction::ToggleDrone:   ToggleModel(EToyModelKind::Drone); break;
	case EToyScreenAction::ToggleLathe:   ToggleModel(EToyModelKind::Lathe); break;
	case EToyScreenAction::ToggleExplode: ToggleModelExploded(); break;
	case EToyScreenAction::ResetParts:    ResetModel(); break;
	case EToyScreenAction::MoveNear:      MoveModel(FVector(-MoveStepCm, 0.f, 0.f)); break;
	case EToyScreenAction::MoveFar:       MoveModel(FVector(MoveStepCm, 0.f, 0.f)); break;
	case EToyScreenAction::MoveLeft:      MoveModel(FVector(0.f, -MoveStepCm, 0.f)); break;
	case EToyScreenAction::MoveRight:     MoveModel(FVector(0.f, MoveStepCm, 0.f)); break;
	case EToyScreenAction::OpenMenu:      if (Menu) { Menu->Open(); } break;
	}
	// 戳按钮时触屏自己也会刷新；调试命令 toy.Screen 不经过触屏，这里统一刷新一次按钮上的字和状态栏
	if (Screen)
	{
		Screen->RefreshStatus();
	}
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
		MissingModel = KindName(Kind);
		return false;
	}
	Spawned->AttachToComponent(Mount, FAttachmentTransformRules::KeepWorldTransform);
	Model = Spawned;
	ModelKind = Kind;
	MissingModel.Reset();
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
	if (!MissingModel.IsEmpty())
	{
		return FString::Printf(TEXT("%s NOT IMPORTED - RUN scripts/import_%s.bat"), *MissingModel, *MissingModel.ToLower());
	}
	const FString Name = !Model ? TEXT("MODEL") : KindName(ModelKind);
	const FString State = !Model ? TEXT("OFF") : (Model->IsExploded() ? TEXT("EXPLODED") : TEXT("ASSEMBLED"));
	return FString::Printf(TEXT("CUBES %d / %d    %s %s    %.0f CM AWAY"), GetCubeCount(), MaxCubes, *Name, *State,
		GetModelDistanceCm());
}

int32 AToyStudio::GetCubeCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AToyCube>& Cube : Cubes)
	{
		Count += Cube.IsValid() ? 1 : 0;
	}
	return Count;
}
