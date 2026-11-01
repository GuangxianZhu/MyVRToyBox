#include "ToyTreasure.h"

#include "ToyBoxPawn.h"
#include "ToyFeedback.h"
#include "ToyHand.h"
#include "ToyLilliput.h"
#include "ToyProp.h"
#include "ToyStudio.h"
#include "ToyZipline.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

// 常量名带 Treasure 前缀：unity 合并编译时别的 .cpp 匿名命名空间里的同名常量会二义
namespace ToyTreasureLocal
{
	/** 小身体走到这么近（小人眼里的厘米，乘比例换成世界厘米）宝物外面开始一闪一闪。 */
	constexpr float TreasureGlowRange = 150.f;
	/** 身体（脚到眼睛那一段）离宝物这么近（小人眼里的厘米）就算碰到。 */
	constexpr float TreasureTouchRadius = 30.f;
	/** 宝物要的头顶空间（世界厘米）：放得下最高的零件。 */
	constexpr float TreasureHeadroom = 2.f;
	constexpr float TreasureGlowDiameter = 3.2f;
	const TCHAR* TreasureGlowMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	const FLinearColor TreasureGlowColor(1.f, 0.8f, 0.3f);

	const FLinearColor TreasureGold(1.f, 0.72f, 0.18f);
	const FLinearColor TreasureSilver(0.82f, 0.84f, 0.88f);
	const FLinearColor TreasureSteel(0.45f, 0.55f, 0.7f);
	const FLinearColor TreasureCopper(0.9f, 0.45f, 0.2f);
	const FLinearColor TreasureGreen(0.15f, 0.75f, 0.3f);
	const FLinearColor TreasureBronze(0.7f, 0.5f, 0.25f);
	const FLinearColor TreasureDark(0.05f, 0.05f, 0.06f);

	enum class ETreasureRoute : uint8
	{
		Any,
		/** 预设滑索落地的那一片地上：从工作台（小人偶开局的地方）滑下去最快 */
		Zipline,
		/** 一步跨不上去（2.5cm）：要用 OBJECTS 的书、瓷砖、木板搭台阶 / 斜坡 */
		Stairs
	};

	/**
	 * 候选藏宝点（世界坐标，样板间 L_StudioArchViz）：从 (X, Y, StartZ) 往下打射线落到表面上；
	 * MaxUp > 0 = "底下"的点，头顶 MaxUp 厘米以内必须有家具盖着（从大人的视角看不见）。
	 */
	struct FTreasureSpot
	{
		const TCHAR* Name;
		float X;
		float Y;
		float StartZ;
		float MaxUp;
		ETreasureRoute Route;
	};

	const FTreasureSpot TreasureSpots[] = {
		{ TEXT("沙发后面的缝里"), -340.f, -305.f, 70.f, 0.f, ETreasureRoute::Any },
		{ TEXT("暖气片下面"), -553.f, -30.f, 12.f, 20.f, ETreasureRoute::Any },
		{ TEXT("电视柜底下"), -300.f, 168.f, 20.f, 45.f, ETreasureRoute::Any },
		{ TEXT("梯架中间的搁板上"), -72.f, 180.f, 260.f, 0.f, ETreasureRoute::Any },
		{ TEXT("梯架顶上"), -72.f, 203.f, 260.f, 0.f, ETreasureRoute::Any },
		{ TEXT("墙上搁板的花瓶后面"), 279.f, 294.f, 175.f, 0.f, ETreasureRoute::Any },
		{ TEXT("电视上面那块小搁板上"), -480.f, 195.f, 260.f, 0.f, ETreasureRoute::Any },
		// 预设滑索落在工作台西边的地上（约 (30, 124)），从这里走过去最近
		{ TEXT("餐厅矮柜下层的格子里"), 100.f, 280.f, 40.f, 50.f, ETreasureRoute::Zipline },
		{ TEXT("梯架和墙之间的地上"), -72.f, 200.f, 20.f, 120.f, ETreasureRoute::Zipline },
		{ TEXT("门厅大盆栽后面的墙角"), 497.f, 323.f, 40.f, 0.f, ETreasureRoute::Any },
		// 30cm 高的小凳子：小身体一步只跨 2.5cm，要用书、盒子、木板搭台阶 / 斜坡上去
		{ TEXT("门厅小凳子上"), 620.f, 20.f, 60.f, 0.f, ETreasureRoute::Stairs },
		{ TEXT("门厅另一个小凳子上"), 840.f, 20.f, 60.f, 0.f, ETreasureRoute::Stairs },
		{ TEXT("窗台上收音机后面"), 165.f, -330.f, 110.f, 0.f, ETreasureRoute::Any },
	};

	AToyBoxPawn* TreasurePawn(const UWorld* World)
	{
		return World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
	}

	/** 射线挑表面时不算的东西：玩家、手、小人偶、大人像、会动的摆件、滑索、宝物自己。 */
	void IgnoreTreasureActors(UWorld* World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<AToyBoxPawn>() || It->IsA<AToyHand>() || It->IsA<AToyProp>() || It->IsA<AToyGiantBody>() || It->IsA<AToyTreasure>()
				|| It->IsA<AToyTreasureRobot>() || It->IsA<AToyZipline>() || It->IsA<AToyZipPulley>())
			{
				Params.AddIgnoredActor(*It);
			}
		}
	}

	/** 候选点落到真实表面上：朝上的面、头顶放得下；"底下"的点头顶要有东西盖着。 */
	bool FindTreasureSurface(UWorld* World, const FTreasureSpot& Spot, const FCollisionQueryParams& Params, FVector& OutPoint, FString& OutWhy)
	{
		const FVector From(Spot.X, Spot.Y, Spot.StartZ);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params))
		{
			OutWhy = TEXT("下面没有表面");
			return false;
		}
		if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f)
		{
			OutWhy = FString::Printf(TEXT("打到的不是朝上的面（%s）"), *GetNameSafe(Hit.GetActor()));
			return false;
		}
		const FVector Point = Hit.ImpactPoint;
		FHitResult Up;
		if (World->LineTraceSingleByChannel(Up, Point + FVector(0.f, 0.f, 0.1f), Point + FVector(0.f, 0.f, TreasureHeadroom), ECC_Visibility, Params))
		{
			OutWhy = FString::Printf(TEXT("头顶放不下（%s）"), *GetNameSafe(Up.GetActor()));
			return false;
		}
		if (Spot.MaxUp > 0.f && !World->LineTraceSingleByChannel(Up, Point + FVector(0.f, 0.f, 0.5f), Point + FVector(0.f, 0.f, Spot.MaxUp), ECC_Visibility, Params))
		{
			OutWhy = FString::Printf(TEXT("头顶 %.0fcm 以内没有东西盖着"), Spot.MaxUp);
			return false;
		}
		OutPoint = Point;
		OutWhy = FString::Printf(TEXT("在 %s 上"), *GetNameSafe(Hit.GetActor()));
		return true;
	}

	UStaticMesh* TreasureMesh(bool bCube, bool bSphere)
	{
		return LoadObject<UStaticMesh>(nullptr, bSphere ? TEXT("/Engine/BasicShapes/Sphere.Sphere")
			: bCube ? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}

	/** 运行时加一个基础形状的小零件（不碰撞），SizeCm 是世界尺寸。 */
	UStaticMeshComponent* AddTreasurePart(AActor* Owner, USceneComponent* Parent, bool bCube, bool bSphere, const FVector& Location,
		const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(TreasureMesh(bCube, bSphere));
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocationAndRotation(Location, Rotation);
		Part->SetRelativeScale3D(SizeCm / 100.f);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(Base, Owner);
			Paint->SetVectorParameterValue(TEXT("Color"), Color);
			Part->SetMaterial(0, Paint);
		}
		Part->RegisterComponent();
		Owner->AddInstanceComponent(Part);
		return Part;
	}

	void ShowTreasureToast(UWorld* World, const FString& Title, const FString& Body)
	{
		if (AToyBoxPawn* Pawn = TreasurePawn(World))
		{
			Pawn->ShowPlayerToast(Title, Body);
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：%s / %s"), *Title, *Body.Replace(TEXT("\n"), TEXT(" ")));
	}

	/** toy.Treasure list | collect N | reset | goto N */
	void TreasureCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : TEXT("list");
		const TArray<AToyTreasure*> All = AToyTreasure::GetAll(World);
		auto Pick = [&](int32& OutIndex) -> AToyTreasure*
		{
			OutIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
			if (!All.IsValidIndex(OutIndex - 1))
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox 寻宝：没有第 %d 个（这一轮 %d 个，从 1 数）"), OutIndex, All.Num());
				return nullptr;
			}
			return All[OutIndex - 1];
		};
		if (Verb == TEXT("list"))
		{
			int32 Found = 0, Total = 0;
			AToyTreasure::GetProgress(World, Found, Total);
			UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：找到 %d / %d"), Found, Total);
			for (const AToyTreasure* Treasure : All)
			{
				UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：  %d. %s · %s %s%s"), Treasure->GetHuntIndex(), AToyTreasure::KindLabel(Treasure->GetKind()),
					*Treasure->GetSpotName(), *Treasure->GetActorLocation().ToCompactString(), Treasure->IsCollected() ? TEXT("（已找到）") : TEXT(""));
			}
		}
		else if (Verb == TEXT("reset"))
		{
			AToyTreasure::StartHunt(World);
		}
		else if (Verb == TEXT("collect"))
		{
			int32 Index = 0;
			if (AToyTreasure* Treasure = Pick(Index))
			{
				Treasure->Collect(nullptr);
			}
		}
		else if (Verb == TEXT("goto"))
		{
			// 小身体站到它旁边、看着它（要先 toy.Shrink 变小）
			int32 Index = 0;
			AToyTreasure* Treasure = Pick(Index);
			AToyBoxPawn* Pawn = TreasurePawn(World);
			if (!Treasure || !Pawn)
			{
				return;
			}
			FVector Feet;
			float Yaw = 0.f;
			if (!Treasure->FindViewPoint(Feet, Yaw))
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox 寻宝：第 %d 个（%s）旁边没有站得下的地方"), Index, *Treasure->GetSpotName());
				return;
			}
			// 眼睛按小人的眼睛高度算，往下看着它
			const FVector Eye = Feet + FVector(0.f, 0.f, Pawn->DesktopEyeHeight * ToyLilliput::GetScale());
			const FVector ToTreasure = Treasure->GetActorLocation() + FVector(0.f, 0.f, 0.5f) - Eye;
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(ToTreasure.Z, ToTreasure.Size2D()));
			if (Pawn->TeleportTinyTo(Feet, Yaw, Pitch))
			{
				UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：站到第 %d 个（%s）旁边 %s，朝 %.0f°，往下 %.0f°"), Index, *Treasure->GetSpotName(),
					*Feet.ToCompactString(), Yaw, -Pitch);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox 寻宝：goto 要先变小（toy.Shrink）"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox 寻宝：toy.Treasure list | collect N | reset | goto N"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TreasureCommandRef(TEXT("toy.Treasure"),
		TEXT("小人国寻宝：toy.Treasure list（位置和找没找到写进日志）/ collect N（直接收下第 N 个）/ reset（重新藏一轮，和菜单 NEW HUNT 一样）/ ")
		TEXT("goto N（小身体站到第 N 个旁边看着它，要先 toy.Shrink）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TreasureCommand));
}

using namespace ToyTreasureLocal;

// ---------------------------------------------------------------- 宝物

AToyTreasure::AToyTreasure()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Glow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Glow->SetCastShadow(false);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 0.6f));
	Glow->SetRelativeScale3D(FVector(TreasureGlowDiameter / 100.f));
	Glow->SetVisibility(false);

	// 捏的检测盒比零件大一圈（小手捏 1cm 的东西不用太准）
	GrabZone = CreateDefaultSubobject<UBoxComponent>(TEXT("GrabZone"));
	GrabZone->SetupAttachment(Root);
	GrabZone->SetRelativeLocation(FVector(0.f, 0.f, 0.6f));
	GrabZone->InitBoxExtent(FVector(1.2f, 1.2f, 0.9f));
	GrabZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GrabZone->SetCollisionObjectType(ECC_WorldDynamic);
	GrabZone->SetCollisionResponseToAllChannels(ECR_Overlap);
}

const TCHAR* AToyTreasure::KindLabel(EToyTreasureKind InKind)
{
	switch (InKind)
	{
	case EToyTreasureKind::Gear:    return TEXT("GEAR");
	case EToyTreasureKind::Screw:   return TEXT("SCREW");
	case EToyTreasureKind::Battery: return TEXT("BATTERY");
	case EToyTreasureKind::Nut:     return TEXT("NUT");
	case EToyTreasureKind::Bolt:    return TEXT("BOLT");
	case EToyTreasureKind::Bearing: return TEXT("BALL BEARING");
	default:                        return TEXT("?");
	}
}

void AToyTreasure::Setup(EToyTreasureKind InKind, int32 InHuntIndex, const FString& InSpotName)
{
	Kind = InKind;
	HuntIndex = InHuntIndex;
	SpotName = InSpotName;
	BuildShape();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TreasureGlowMaterialPath))
	{
		Glow->SetStaticMesh(TreasureMesh(false, true));
		GlowMaterial = UMaterialInstanceDynamic::Create(Base, this);
		GlowMaterial->SetVectorParameterValue(TEXT("Color"), TreasureGlowColor * 6.f);
		GlowMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.f);
		Glow->SetMaterial(0, GlowMaterial);
	}
	// 每个转一个随机的角度，看起来像随手丢在那里的
	SetActorRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
}

UStaticMeshComponent* AToyTreasure::AddPart(bool bCube, bool bSphere, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color)
{
	return AddTreasurePart(this, Root, bCube, bSphere, Location, Rotation, SizeCm, Color);
}

void AToyTreasure::BuildShape()
{
	// 尺寸是世界厘米（小人眼里放大 12 倍）；引擎圆柱的轴是 Z，躺着放转 Pitch 90°
	const FRotator Lying(90.f, 0.f, 0.f);
	switch (Kind)
	{
	case EToyTreasureKind::Gear:
		AddPart(false, false, FVector(0.f, 0.f, 0.2f), FRotator::ZeroRotator, FVector(1.5f, 1.5f, 0.4f), TreasureGold);
		for (int32 Tooth = 0; Tooth < 8; ++Tooth)
		{
			const float Angle = Tooth * 45.f;
			AddPart(true, false, FRotator(0.f, Angle, 0.f).RotateVector(FVector(0.8f, 0.f, 0.2f)), FRotator(0.f, Angle, 0.f), FVector(0.4f, 0.35f, 0.4f), TreasureGold);
		}
		AddPart(false, false, FVector(0.f, 0.f, 0.42f), FRotator::ZeroRotator, FVector(0.4f, 0.4f, 0.05f), TreasureDark);
		break;
	case EToyTreasureKind::Screw:
		AddPart(false, false, FVector(0.2f, 0.f, 0.17f), Lying, FVector(0.3f, 0.3f, 1.4f), TreasureSilver);
		AddPart(false, false, FVector(-0.6f, 0.f, 0.35f), Lying, FVector(0.7f, 0.7f, 0.25f), TreasureSilver);
		AddPart(true, false, FVector(-0.74f, 0.f, 0.35f), FRotator::ZeroRotator, FVector(0.03f, 0.5f, 0.08f), TreasureDark);
		break;
	case EToyTreasureKind::Battery:
		AddPart(false, false, FVector(0.f, 0.f, 0.35f), Lying, FVector(0.7f, 0.7f, 1.6f), TreasureGreen);
		AddPart(false, false, FVector(0.6f, 0.f, 0.35f), Lying, FVector(0.72f, 0.72f, 0.4f), TreasureCopper);
		AddPart(false, false, FVector(0.86f, 0.f, 0.35f), Lying, FVector(0.3f, 0.3f, 0.15f), TreasureSilver);
		break;
	case EToyTreasureKind::Nut:
		AddPart(false, false, FVector(0.f, 0.f, 0.22f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, 0.45f), TreasureBronze);
		AddPart(false, false, FVector(0.f, 0.f, 0.45f), FRotator::ZeroRotator, FVector(0.5f, 0.5f, 0.02f), TreasureDark);
		break;
	case EToyTreasureKind::Bolt:
		AddPart(false, false, FVector(0.25f, 0.f, 0.22f), Lying, FVector(0.45f, 0.45f, 1.5f), TreasureSteel);
		AddPart(true, false, FVector(-0.65f, 0.f, 0.4f), FRotator::ZeroRotator, FVector(0.4f, 0.8f, 0.8f), TreasureSteel);
		break;
	case EToyTreasureKind::Bearing:
	default:
		AddPart(false, true, FVector(0.f, 0.f, 0.45f), FRotator::ZeroRotator, FVector(0.9f), TreasureSilver);
		break;
	}
}

int32 AToyTreasure::StartHunt(UWorld* World)
{
	if (!World)
	{
		return 0;
	}
	for (TActorIterator<AToyTreasure> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AToyTreasureRobot> It(World); It; ++It)
	{
		It->Destroy();
	}

	// 一个个验证候选点（家具被换过、没生成新版样板间时有的会落空）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTreasureSpot), false);
	IgnoreTreasureActors(World, Params);
	TArray<TPair<int32, FVector>> Valid;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TreasureSpots); ++Index)
	{
		FVector Point = FVector::ZeroVector;
		FString Why;
		const bool bOk = FindTreasureSurface(World, TreasureSpots[Index], Params, Point, Why);
		if (bOk)
		{
			Valid.Emplace(Index, Point);
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：候选 %d「%s」%s：%s %s"), Index, TreasureSpots[Index].Name, bOk ? TEXT("可以") : TEXT("不行"), *Why,
			bOk ? *Point.ToCompactString() : TEXT(""));
	}

	// 先各挑一个滑索那一片的、一个要搭台阶的，剩下的随机
	TArray<TPair<int32, FVector>> Chosen;
	for (const ETreasureRoute Route : { ETreasureRoute::Zipline, ETreasureRoute::Stairs })
	{
		TArray<int32> OfRoute;
		for (int32 I = 0; I < Valid.Num(); ++I)
		{
			if (TreasureSpots[Valid[I].Key].Route == Route)
			{
				OfRoute.Add(I);
			}
		}
		if (OfRoute.Num() > 0)
		{
			const int32 Pick = OfRoute[FMath::RandHelper(OfRoute.Num())];
			Chosen.Add(Valid[Pick]);
			Valid.RemoveAt(Pick);
		}
	}
	while (Chosen.Num() < HuntSize && Valid.Num() > 0)
	{
		const int32 Pick = FMath::RandHelper(Valid.Num());
		Chosen.Add(Valid[Pick]);
		Valid.RemoveAt(Pick);
	}

	// 种类也打乱，每轮在哪儿找到什么不一样
	TArray<int32> Kinds;
	for (int32 K = 0; K < static_cast<int32>(EToyTreasureKind::Count); ++K)
	{
		Kinds.Insert(K, FMath::RandHelper(Kinds.Num() + 1));
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 Count = 0;
	for (int32 I = 0; I < Chosen.Num(); ++I)
	{
		AToyTreasure* Treasure = World->SpawnActor<AToyTreasure>(StaticClass(), FTransform(Chosen[I].Value), SpawnParams);
		if (Treasure)
		{
			Treasure->Setup(static_cast<EToyTreasureKind>(Kinds[I % Kinds.Num()]), I + 1, TreasureSpots[Chosen[I].Key].Name);
			++Count;
			UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：%d. %s 藏在%s %s"), I + 1, KindLabel(Treasure->Kind), *Treasure->SpotName, *Chosen[I].Value.ToCompactString());
		}
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：新的一轮，藏了 %d 个"), Count);
	return Count;
}

TArray<AToyTreasure*> AToyTreasure::GetAll(UWorld* World)
{
	TArray<AToyTreasure*> All;
	if (World)
	{
		for (TActorIterator<AToyTreasure> It(World); It; ++It)
		{
			All.Add(*It);
		}
	}
	All.Sort([](const AToyTreasure& L, const AToyTreasure& R) { return L.HuntIndex < R.HuntIndex; });
	return All;
}

void AToyTreasure::GetProgress(UWorld* World, int32& OutFound, int32& OutTotal)
{
	OutFound = 0;
	OutTotal = 0;
	for (const AToyTreasure* Treasure : GetAll(World))
	{
		++OutTotal;
		OutFound += Treasure->bCollected ? 1 : 0;
	}
}

void AToyTreasure::Collect(AToyHand* Hand)
{
	if (bCollected)
	{
		return;
	}
	bCollected = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	int32 Found = 0, Total = 0;
	GetProgress(GetWorld(), Found, Total);
	if (UToyFeedback* System = UToyFeedback::Get(this))
	{
		FToyFeedbackParams Params;
		Params.Location = GetActorLocation();
		Params.Hand = Hand ? Hand->GetSide() : EControllerHand::AnyHand;
		Params.Material = EToyMaterial::LightMetal;
		Params.SizeCm3 = 2.f;
		Params.Source = this;
		System->Play(EToyFeedback::MagnetSnap, Params);
		System->Play(EToyFeedback::GrabSecured, Params);
	}
	AToyBoxPawn* Pawn = TreasurePawn(GetWorld());
	if (Pawn && !Hand)
	{
		// 身体碰到的：两只手各震一下
		Pawn->PlayHaptic(EControllerHand::Left, 0.6f, 0.08f);
		Pawn->PlayHaptic(EControllerHand::Right, 0.6f, 0.08f);
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：找到 %s（%s），%d / %d"), KindLabel(Kind), *SpotName, Found, Total);

	if (Found < Total)
	{
		ShowTreasureToast(GetWorld(), FString::Printf(TEXT("FOUND %d/%d"), Found, Total), FString::Printf(TEXT("%s · %s"), KindLabel(Kind), *SpotName));
		return;
	}
	// 找齐了：工作台上出现小机器人
	AToyTreasureRobot::SpawnOnWorkbench(GetWorld());
	ShowTreasureToast(GetWorld(), FString::Printf(TEXT("ALL %d FOUND!"), Total), TEXT("六个零件拼成了一个小机器人，在工作台上\n菜单 TREASURE 按钮 = 再藏一轮"));
	if (UToyFeedback* System = UToyFeedback::Get(this))
	{
		FToyFeedbackParams Params;
		Params.Location = GetActorLocation();
		Params.Material = EToyMaterial::LightMetal;
		Params.SizeCm3 = 50.f;
		System->Play(EToyFeedback::Seat, Params);
	}
	if (Pawn)
	{
		Pawn->PlayHaptic(EControllerHand::Left, 0.8f, 0.25f);
		Pawn->PlayHaptic(EControllerHand::Right, 0.8f, 0.25f);
	}
}

void AToyTreasure::ReleaseHandNextTick(AToyHand* Hand)
{
	TWeakObjectPtr<AToyHand> WeakHand(Hand);
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakHand]()
	{
		if (AToyHand* Holder = WeakHand.Get(); Holder && Holder->GetHeldActor() == this)
		{
			Holder->ReleaseHeld(false);
		}
	}));
}

void AToyTreasure::OnGrabbed(AToyHand* Hand)
{
	ReleaseHandNextTick(Hand);
	if (ToyLilliput::GetScale() >= 1.f)
	{
		ShowTreasureToast(GetWorld(), TEXT("TOO BIG - SHRINK TO PICK UP"), TEXT("左手 Y 变小再来捡"));
		return;
	}
	Collect(Hand);
}

void AToyTreasure::AddHover()
{
	++HoverCount;
}

void AToyTreasure::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
}

void AToyTreasure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AToyBoxPawn* Pawn = TreasurePawn(GetWorld());
	if (bCollected || !Pawn)
	{
		return;
	}
	const float Scale = ToyLilliput::GetScale();
	const bool bTiny = Pawn->IsTiny() && Scale < 1.f;
	const FVector Eye = Pawn->Camera->GetComponentLocation();
	const FVector Feet(Eye.X, Eye.Y, Pawn->GetActorLocation().Z);
	const FVector Center = GetActorLocation() + FVector(0.f, 0.f, 0.5f);

	// 小身体碰到就收下（脚到眼睛这一段离它够近）；滑索上路过不算
	const float Distance = FMath::PointDistToSegment(Center, Feet, Eye);
	if (bTiny && !Pawn->IsZipRiding() && Distance < TreasureTouchRadius * Scale)
	{
		Collect(nullptr);
		return;
	}

	// 小身体走近了（离身体 1.5 米）：外面一圈光一闪一闪（离得越近越亮）；手指着它时亮一点
	const float Range = TreasureGlowRange * Scale;
	const bool bShowGlow = bTiny && Distance < Range && GlowMaterial;
	Glow->SetVisibility(bShowGlow);
	if (bShowGlow)
	{
		GlowTime += DeltaSeconds;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(GlowTime * 4.f);
		const float Near = 1.f - Distance / Range;
		GlowMaterial->SetScalarParameterValue(TEXT("Opacity"), (0.08f + 0.27f * Pulse) * (0.4f + 0.6f * Near) * (HoverCount > 0 ? 1.6f : 1.f));
	}
}

bool AToyTreasure::FindViewPoint(FVector& OutFeet, float& OutYaw) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTreasureView), false);
	IgnoreTreasureActors(World, Params);
	const float Scale = ToyLilliput::TinyScale;
	const FVector Base = GetActorLocation();
	// 离它 12–6cm（小人眼里 1.4–0.7 米），16 个方向，挑第一个脚下有地、站得下（头顶不撞家具）的
	for (const float Distance : { 12.f, 9.f, 6.f })
	{
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Dir = FRotator(0.f, Step * 22.5f, 0.f).Vector();
			const FVector Probe = Base + Dir * Distance;
			FHitResult Ground;
			if (!World->LineTraceSingleByChannel(Ground, Probe + FVector(0.f, 0.f, 3.f), Probe - FVector(0.f, 0.f, 3.f), ECC_Visibility, Params)
				|| Ground.bStartPenetrating || Ground.ImpactNormal.Z < 0.7f)
			{
				continue;
			}
			const FVector Feet = Ground.ImpactPoint;
			// 身体：脚上方 3cm 到眼睛（165 × 比例）一根粗 1.6cm 的柱子扫一遍；从它到身体之间也不能隔着墙
			FHitResult Block;
			const float Radius = 0.8f;
			if (World->SweepSingleByChannel(Block, Feet + FVector(0.f, 0.f, 3.f), Feet + FVector(0.f, 0.f, 165.f * Scale), FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeSphere(Radius), Params)
				|| World->LineTraceSingleByChannel(Block, Feet + FVector(0.f, 0.f, 1.f), Base + FVector(0.f, 0.f, 1.f), ECC_Visibility, Params))
			{
				continue;
			}
			OutFeet = Feet;
			OutYaw = (-Dir).Rotation().Yaw;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- 找齐以后的小机器人

AToyTreasureRobot::AToyTreasureRobot()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
}

AToyTreasureRobot* AToyTreasureRobot::SpawnOnWorkbench(UWorld* World)
{
	AToyStudio* Studio = World ? AToyStudio::Find(World) : nullptr;
	if (!Studio)
	{
		return nullptr;
	}
	// 工作台面靠里的角上（工作室坐标；靠前那头有滑索的柱子和绳子），往下找台面
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyTreasureRobot), false);
	IgnoreTreasureActors(World, Params);
	const FVector From = Studio->GetActorTransform().TransformPosition(FVector(100.f, 62.f, 250.f));
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 面朝站立点（工作室 -X）
	AToyTreasureRobot* Robot = World->SpawnActor<AToyTreasureRobot>(StaticClass(),
		FTransform(FRotator(0.f, Studio->GetActorRotation().Yaw + 180.f, 0.f), Hit.ImpactPoint), SpawnParams);
	if (Robot)
	{
		Robot->Build();
		UE_LOG(LogTemp, Log, TEXT("ToyBox 寻宝：小机器人出现在 %s（%s 上）"), *Hit.ImpactPoint.ToCompactString(), *GetNameSafe(Hit.GetActor()));
	}
	return Robot;
}

void AToyTreasureRobot::AddPart(bool bCube, bool bSphere, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color)
{
	AddTreasurePart(this, Root, bCube, bSphere, Location, Rotation, SizeCm, Color);
}

void AToyTreasureRobot::Build()
{
	// 12cm 高的小机器人（小人眼里 1.4 米），+X 朝前：螺栓当腿、电池当身体、螺母当肩膀、齿轮在胸口、钢珠当眼睛、螺丝当天线
	const FRotator Lying(90.f, 0.f, 0.f);
	const FRotator Sideways(0.f, 0.f, 90.f);
	for (const float Side : { -1.f, 1.f })
	{
		AddPart(false, false, FVector(0.f, Side * 1.3f, 1.8f), FRotator::ZeroRotator, FVector(0.9f, 0.9f, 3.6f), TreasureSteel);       // 腿
		AddPart(true, false, FVector(0.4f, Side * 1.3f, 0.3f), FRotator::ZeroRotator, FVector(2.f, 1.3f, 0.6f), TreasureSteel);        // 脚
		AddPart(false, false, FVector(0.f, Side * 2.6f, 6.6f), Sideways, FVector(1.4f, 1.4f, 0.7f), TreasureBronze);                   // 肩膀
		AddPart(false, false, FVector(0.f, Side * 2.9f, 5.f), FRotator::ZeroRotator, FVector(0.6f, 0.6f, 3.f), TreasureSilver);        // 胳膊
		AddPart(false, true, FVector(1.1f, Side * 0.75f, 9.4f), FRotator::ZeroRotator, FVector(0.8f), TreasureSilver);                 // 眼睛
	}
	AddPart(false, false, FVector(0.f, 0.f, 5.3f), FRotator::ZeroRotator, FVector(3.8f, 3.8f, 3.6f), TreasureGreen);                  // 电池身体
	AddPart(false, false, FVector(0.f, 0.f, 7.25f), FRotator::ZeroRotator, FVector(3.9f, 3.9f, 0.3f), TreasureCopper);
	AddPart(false, false, FVector(1.9f, 0.f, 5.3f), Lying, FVector(2.f, 2.f, 0.3f), TreasureGold);                                   // 胸口的齿轮
	AddPart(true, false, FVector(0.f, 0.f, 9.1f), FRotator::ZeroRotator, FVector(2.2f, 2.8f, 2.4f), TreasureSilver);                // 头
	AddPart(true, false, FVector(1.11f, 0.f, 8.4f), FRotator::ZeroRotator, FVector(0.05f, 1.4f, 0.25f), TreasureDark);               // 嘴
	AddPart(false, false, FVector(0.f, 0.f, 11.f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, 1.6f), TreasureSilver);             // 天线
	AddPart(false, true, FVector(0.f, 0.f, 11.9f), FRotator::ZeroRotator, FVector(0.5f), TreasureGold);
}

void AToyTreasureRobot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 出场先蹦一下，然后一直慢慢转
	Age += DeltaSeconds;
	AddActorWorldRotation(FRotator(0.f, 25.f * DeltaSeconds, 0.f));
	const float Hop = Age < 1.2f ? FMath::Abs(FMath::Sin(Age * PI * 2.5f)) * 1.5f * (1.f - Age / 1.2f) : 0.f;
	if (Hop != LastHop)
	{
		AddActorWorldOffset(FVector(0.f, 0.f, Hop - LastHop));
		LastHop = Hop;
	}
}
