#include "ToyStudio.h"

#include "ToyCube.h"
#include "ToyExplodedModel.h"
#include "ToyShapeComponent.h"
#include "ToyTouchScreen.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
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

	// 电机：桌面中间偏里。Yaw -90 让电机轴（模型 X）沿桌子长边，轴伸端朝左手边，
	// 爆炸时轴伸端那侧（转子、前端盖）飞得最远，往左边飞就不会撞到右边的触屏
	MotorMount = CreateDefaultSubobject<USceneComponent>(TEXT("MotorMount"));
	MotorMount->SetupAttachment(Root);
	MotorMount->SetRelativeLocationAndRotation(FVector(TableX + 15.f, -10.f, TableHeight), FRotator(0.f, -90.f, 0.f));

	// 球阀：同一个位置，Yaw 90。球阀模型的 X（管道方向 = 执行器缸体长轴）转到桌子长边上，端盖、活塞往左右两边飞，
	// 不会朝玩家脸上飞；电磁阀（模型 +Y）转到朝着玩家的一面
	ValveMount = CreateDefaultSubobject<USceneComponent>(TEXT("ValveMount"));
	ValveMount->SetupAttachment(Root);
	ValveMount->SetRelativeLocationAndRotation(FVector(TableX + 15.f, -10.f, TableHeight), FRotator(0.f, 90.f, 0.f));

	// 无人机：同一个位置。模型 +Y 是机头（相机那边），Yaw 90 让机头朝向玩家（-X）
	DroneMount = CreateDefaultSubobject<USceneComponent>(TEXT("DroneMount"));
	DroneMount->SetupAttachment(Root);
	DroneMount->SetRelativeLocationAndRotation(FVector(TableX + 15.f, -10.f, TableHeight), FRotator(0.f, 90.f, 0.f));

	// 触屏：桌面右手边靠前，转过来正对玩家站立点
	const FVector ScreenLocation(TableX - 20.f, 60.f, TableHeight);
	ScreenMount = CreateDefaultSubobject<USceneComponent>(TEXT("ScreenMount"));
	ScreenMount->SetupAttachment(Root);
	ScreenMount->SetRelativeLocationAndRotation(ScreenLocation, FVector(-ScreenLocation.X, -ScreenLocation.Y, 0.f).Rotation());
}

void AToyStudio::BeginPlay()
{
	Super::BeginPlay();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Screen = GetWorld()->SpawnActor<AToyTouchScreen>(AToyTouchScreen::StaticClass(), ScreenMount->GetComponentTransform(), Params);
	if (Screen)
	{
		Screen->AttachToComponent(ScreenMount, FAttachmentTransformRules::KeepWorldTransform);
		Screen->SetStudio(this);
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
	case EToyScreenAction::ToggleExplode: ToggleModelExploded(); break;
	case EToyScreenAction::ResetParts:    ResetModel(); break;
	}
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

	USceneComponent* Mount = Kind == EToyModelKind::Motor ? MotorMount : (Kind == EToyModelKind::Valve ? ValveMount : DroneMount);
	const FString& Manifest = Kind == EToyModelKind::Motor ? MotorManifest : (Kind == EToyModelKind::Valve ? ValveManifest : DroneManifest);
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
	if (!Spawned->LoadManifest(FPaths::ProjectContentDir() / Manifest))
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
	return FString::Printf(TEXT("CUBES %d / %d     %s %s"), GetCubeCount(), MaxCubes, *Name, *State);
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
