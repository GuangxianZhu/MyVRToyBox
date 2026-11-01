#include "ToyZipline.h"

#include "ToyBoxPawn.h"
#include "ToyHand.h"
#include "ToyLilliput.h"
#include "ToyStudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// 常量名带 Zip 前缀：unity 合并编译时别的 .cpp 匿名命名空间里的同名常量会二义
namespace ToyZiplineLocal
{
	/** 朝上的表面上立的柱子高度（厘米）：绳子挂在柱顶，把手挂在下面 3cm，正好在小人头顶上方一点、伸手够得到。 */
	constexpr float ZipPostHeight = 17.f;
	/** 墙上钉的短桩伸出来多长。 */
	constexpr float ZipPegLength = 2.f;
	constexpr float ZipPostDiameter = 1.2f;
	constexpr float ZipRopeDiameter = 0.4f;
	/** 最长 3 米、两头至少差 5cm 高。 */
	constexpr float ZipMaxLength = 300.f;
	constexpr float ZipMinDrop = 5.f;
	/** 把手横杆在绳子下方多远（厘米）。 */
	constexpr float ZipHandleDrop = 3.f;
	/** 线轴松手时离表面多近算"贴着"。 */
	constexpr float ZipSpoolReach = 6.f;
	/** 预设滑索离大人像（站立点）至少这么远，小人滑下去不会穿过人像。 */
	constexpr float ZipPresetStatueClearance = 40.f;

	const FLinearColor ZipRopeColor(0.85f, 0.8f, 0.65f);
	const FLinearColor ZipPostColor(0.35f, 0.25f, 0.15f);
	const FLinearColor ZipPulleyColor(0.9f, 0.35f, 0.05f);
	const FLinearColor ZipHoverColor(1.f, 0.85f, 0.2f);
	const FLinearColor ZipSpoolColor(0.8f, 0.12f, 0.1f);

	int64 GZipSerial = 0;

	UStaticMesh* ZipCylinder()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}

	/** 基础形状的组件：引擎圆柱 + 上色，不碰撞、不跟父组件缩放（世界变换自己摆）。 */
	UStaticMeshComponent* MakeZipPart(AActor* Owner, FName Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCastShadow(true);
		return Part;
	}

	void PaintZipPart(UStaticMeshComponent* Part, const FLinearColor& Color, UObject* Outer)
	{
		if (!Part->GetStaticMesh())
		{
			Part->SetStaticMesh(ZipCylinder());
		}
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(Base, Outer);
			Paint->SetVectorParameterValue(TEXT("Color"), Color);
			Part->SetMaterial(0, Paint);
		}
	}

	/** 把一根引擎圆柱（100 单位高、Z 轴向上）摆成从 From 到 To、直径 Diameter 的棍子。 */
	void PlaceZipStick(UStaticMeshComponent* Part, const FVector& From, const FVector& To, float Diameter)
	{
		const FVector Axis = To - From;
		const float Length = FMath::Max(0.01f, static_cast<float>(Axis.Size()));
		Part->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Axis / Length).ToQuat(), (From + To) * 0.5f,
			FVector(Diameter / 100.f, Diameter / 100.f, Length / 100.f)));
	}

	/** 射线挑表面时不算的东西：滑索、把手、线轴、玩家、手、小人偶和大人像。 */
	void IgnoreZipActors(UWorld* World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<AToyZipline>() || It->IsA<AToyZipPulley>() || It->IsA<AToyZipSpool>() || It->IsA<AToyHand>()
				|| It->IsA<AToyBoxPawn>() || It->IsA<AToyFigurine>() || It->IsA<AToyGiantBody>())
			{
				Params.AddIgnoredActor(*It);
			}
		}
	}

	AToyBoxPawn* ZipPawn(const UWorld* World)
	{
		return World ? Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
	}

	/** toy.Zip preset | clear | line <X1> <Y1> <X2> <Y2> | ride */
	void ZipCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() == 0)
		{
			return;
		}
		const FString Verb = Args[0].ToLower();
		if (Verb == TEXT("clear"))
		{
			UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：删掉 %d 根"), AToyZipline::ClearAll(World));
		}
		else if (Verb == TEXT("preset"))
		{
			AToyZipline::CreatePreset(World);
		}
		else if (Verb == TEXT("line") && Args.Num() >= 5)
		{
			// 工作台坐标的两个点：各自往下找最高的表面，在那里立柱子
			AToyStudio* Studio = AToyStudio::Find(World);
			if (!Studio)
			{
				return;
			}
			const FTransform StudioTransform = Studio->GetActorTransform();
			FVector Points[2];
			for (int32 End = 0; End < 2; ++End)
			{
				const FVector Top = StudioTransform.TransformPosition(FVector(FCString::Atof(*Args[1 + End * 2]), FCString::Atof(*Args[2 + End * 2]), 250.f));
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyZipCommand), false);
				IgnoreZipActors(World, Params);
				FHitResult Hit;
				if (!World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
				{
					UE_LOG(LogTemp, Warning, TEXT("ToyBox 滑索：(%s, %s) 下面没有表面"), *Args[1 + End * 2], *Args[2 + End * 2]);
					return;
				}
				Points[End] = Hit.ImpactPoint;
			}
			FString Reason;
			if (!AToyZipline::Create(World, Points[0], FVector::UpVector, Points[1], FVector::UpVector, Reason))
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox 滑索：拉不成（%s）"), *Reason);
			}
		}
		else if (Verb == TEXT("ride"))
		{
			// 桌面测试：不用手，把小身体挂到最近一根滑索的把手下面直接滑（要先 toy.Shrink 变小）
			AToyBoxPawn* Pawn = ZipPawn(World);
			AToyZipline* Line = AToyZipline::FindLatest(World);
			if (!Pawn || !Line)
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox 滑索：没有玩家或者没有滑索"));
				return;
			}
			Pawn->StartZipRide(Line, nullptr);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox 滑索：toy.Zip preset | clear | line X1 Y1 X2 Y2 | ride"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ZipCommandRef(TEXT("toy.Zip"),
		TEXT("滑索：toy.Zip preset（开局那根预设的）/ clear（全删）/ line X1 Y1 X2 Y2（工作台坐标两点，各自落到最高的表面上立柱子）/ ")
		TEXT("ride（桌面测试：小身体不用手直接挂到最近一根的把手下面滑，路径写进日志）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ZipCommand));
}

using namespace ToyZiplineLocal;

// ---------------------------------------------------------------- 滑索

AToyZipline::AToyZipline()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Rope = MakeZipPart(this, TEXT("Rope"), Root);
	PostTop = MakeZipPart(this, TEXT("PostTop"), Root);
	PostBottom = MakeZipPart(this, TEXT("PostBottom"), Root);
	for (UStaticMeshComponent* Part : { Rope.Get(), PostTop.Get(), PostBottom.Get() })
	{
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
	}
	Rope->SetCastShadow(false);
}

FVector AToyZipline::AttachPoint(const FVector& Point, const FVector& Normal)
{
	// 朝上的面立柱子；墙、柜子侧面这种钉一个短桩，绳子挂在桩头
	return Normal.Z > 0.7f ? Point + FVector(0.f, 0.f, ZipPostHeight) : Point + Normal.GetSafeNormal() * ZipPegLength;
}

AToyZipline* AToyZipline::Create(UWorld* World, const FVector& PointA, const FVector& NormalA, const FVector& PointB, const FVector& NormalB,
	FString& OutReason, bool bInPreset)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector A = AttachPoint(PointA, NormalA);
	const FVector B = AttachPoint(PointB, NormalB);
	const float Length = static_cast<float>(FVector::Dist(A, B));
	if (Length > ZipMaxLength)
	{
		OutReason = FString::Printf(TEXT("TOO LONG (%.1f M, MAX 3 M)"), Length / 100.f);
		return nullptr;
	}
	if (FMath::Abs(A.Z - B.Z) < ZipMinDrop)
	{
		OutReason = TEXT("TOO FLAT - ONE END MUST BE HIGHER");
		return nullptr;
	}
	// 绳子不能穿过家具、墙
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyZipClear), false);
	IgnoreZipActors(World, Params);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, A, B, ECC_Visibility, Params))
	{
		OutReason = FString::Printf(TEXT("BLOCKED BY %s"), *GetNameSafe(Hit.GetActor()).ToUpper());
		UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：%s → %s 被 %s 挡住（%s）"), *A.ToCompactString(), *B.ToCompactString(),
			*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString());
		return nullptr;
	}

	// 满了：拿掉最早的
	TArray<AToyZipline*> Lines;
	for (TActorIterator<AToyZipline> It(World); It; ++It)
	{
		Lines.Add(*It);
	}
	if (Lines.Num() >= MaxLines)
	{
		Lines.Sort([](const AToyZipline& L, const AToyZipline& R) { return L.Serial < R.Serial; });
		Lines[0]->Destroy();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToyZipline* Line = World->SpawnActor<AToyZipline>(StaticClass(), FTransform((A + B) * 0.5f), SpawnParams);
	if (!Line)
	{
		return nullptr;
	}
	Line->Serial = ++GZipSerial;
	Line->bPreset = bInPreset;
	Line->Build(PointA, NormalA, PointB, NormalB);
	UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：%s → %s，长 %.0fcm，落差 %.0fcm，坡度 %.0f°%s"), *Line->Top.ToCompactString(), *Line->Bottom.ToCompactString(),
		Line->Length, Line->Top.Z - Line->Bottom.Z, FMath::RadiansToDegrees(FMath::Asin(-Line->Direction.Z)), bInPreset ? TEXT("（预设）") : TEXT(""));
	return Line;
}

void AToyZipline::Build(const FVector& PointA, const FVector& NormalA, const FVector& PointB, const FVector& NormalB)
{
	const FVector A = AttachPoint(PointA, NormalA);
	const FVector B = AttachPoint(PointB, NormalB);
	const bool bAHigher = A.Z >= B.Z;
	Top = bAHigher ? A : B;
	Bottom = bAHigher ? B : A;
	Length = static_cast<float>(FVector::Dist(Top, Bottom));
	Direction = (Bottom - Top).GetSafeNormal();

	PaintZipPart(Rope, ZipRopeColor, this);
	PaintZipPart(PostTop, ZipPostColor, this);
	PaintZipPart(PostBottom, ZipPostColor, this);
	PlaceZipStick(Rope, Top, Bottom, ZipRopeDiameter);
	PlaceZipStick(PostTop, bAHigher ? PointA : PointB, Top, ZipPostDiameter);
	PlaceZipStick(PostBottom, bAHigher ? PointB : PointA, Bottom, ZipPostDiameter);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Pulley = GetWorld()->SpawnActor<AToyZipPulley>(AToyZipPulley::StaticClass(), FTransform(Top), SpawnParams);
	if (Pulley)
	{
		Pulley->Init(this);
	}
}

void AToyZipline::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 正在滑的时候绳子被删了（CLEAR ALL）：人掉下去，把手一起删
	if (Pulley)
	{
		if (AToyHand* Hand = Pulley->GetHolder())
		{
			Hand->ReleaseHeld(false);
		}
		if (AToyBoxPawn* Pawn = ZipPawn(GetWorld()); Pawn && Pawn->IsZipRiding(this))
		{
			Pawn->StopZipRide(false);
		}
		Pulley->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

int32 AToyZipline::ClearAll(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AToyZipline> It(World); It; ++It)
	{
		It->Destroy();
		++Count;
	}
	return Count;
}

AToyZipline* AToyZipline::FindLatest(UWorld* World)
{
	AToyZipline* Latest = nullptr;
	for (TActorIterator<AToyZipline> It(World); It; ++It)
	{
		if (!Latest || It->Serial > Latest->Serial)
		{
			Latest = *It;
		}
	}
	return Latest;
}

AToyZipline* AToyZipline::CreatePreset(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AToyZipline> It(World); It; ++It)
	{
		if (It->bPreset)
		{
			return *It;
		}
	}
	AToyStudio* Studio = AToyStudio::Find(World);
	if (!Studio)
	{
		return nullptr;
	}
	// 高的一头：工作台面上、小人偶开局站的地方旁边（工作室坐标；台面 X 32..118、Y -76..76，避开台上的工具）；
	// 低的一头：地上几个候选点，挑第一条不穿家具、不太长、离大人像（站立点）够远的
	const FTransform StudioTransform = Studio->GetActorTransform();
	const FVector2D Tops[] = { { 46.f, 42.f }, { 46.f, -8.f } };
	const FVector2D Bottoms[] = { { -60.f, 190.f }, { -110.f, 130.f }, { -140.f, 80.f }, { 20.f, 230.f }, { -150.f, -60.f }, { -60.f, -190.f } };
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyZipPreset), false);
	IgnoreZipActors(World, Params);
	auto SurfaceBelow = [&](const FVector2D& XY, FVector& OutPoint, FVector& OutNormal)
	{
		const FVector From = StudioTransform.TransformPosition(FVector(XY.X, XY.Y, 250.f));
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params) || Hit.ImpactNormal.Z < 0.7f)
		{
			return false;
		}
		OutPoint = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal;
		return true;
	};
	const FVector Statue = Studio->GetActorLocation();
	for (const FVector2D& TopXY : Tops)
	{
		FVector TopPoint, TopNormal;
		if (!SurfaceBelow(TopXY, TopPoint, TopNormal) || TopPoint.Z - Statue.Z < 50.f)
		{
			continue;   // 没打到台面（工作台没摆？）
		}
		for (const FVector2D& BottomXY : Bottoms)
		{
			FVector BottomPoint, BottomNormal;
			if (!SurfaceBelow(BottomXY, BottomPoint, BottomNormal))
			{
				continue;
			}
			// 离站立点（大人像）的水平距离
			const FVector A = AttachPoint(TopPoint, TopNormal);
			const FVector B = AttachPoint(BottomPoint, BottomNormal);
			const FVector Closest = FMath::ClosestPointOnSegment(FVector(Statue.X, Statue.Y, 0.f), FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f));
			if (FVector::Dist2D(Closest, Statue) < ZipPresetStatueClearance)
			{
				continue;
			}
			FString Reason;
			if (AToyZipline* Line = Create(World, TopPoint, TopNormal, BottomPoint, BottomNormal, Reason, true))
			{
				return Line;
			}
			UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：预设候选 (%.0f, %.0f) → (%.0f, %.0f) 不行：%s"), TopXY.X, TopXY.Y, BottomXY.X, BottomXY.Y, *Reason);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("ToyBox 滑索：没找到能拉预设滑索的地方"));
	return nullptr;
}

// ---------------------------------------------------------------- 滑轮把手

AToyZipPulley::AToyZipPulley()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	// 尺寸是世界厘米（小人眼里放大 12 倍）：轮子 1.6cm，挂杆 3cm，横杆 3cm 长，够小手捏
	Wheel = MakeZipPart(this, TEXT("Wheel"), Root);
	Wheel->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.f, 0.f, 90.f));
	Wheel->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.5f) / 100.f);
	Rod = MakeZipPart(this, TEXT("Rod"), Root);
	Rod->SetRelativeLocation(FVector(0.f, 0.f, -ZipHandleDrop * 0.5f));
	Rod->SetRelativeScale3D(FVector(0.3f, 0.3f, ZipHandleDrop) / 100.f);
	Bar = MakeZipPart(this, TEXT("Bar"), Root);
	Bar->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -ZipHandleDrop), FRotator(0.f, 0.f, 90.f));
	Bar->SetRelativeScale3D(FVector(0.5f, 0.5f, 3.f) / 100.f);
	GrabZone = CreateDefaultSubobject<UBoxComponent>(TEXT("GrabZone"));
	GrabZone->SetupAttachment(Root);
	GrabZone->SetRelativeLocation(FVector(0.f, 0.f, -ZipHandleDrop + 0.3f));
	GrabZone->InitBoxExtent(FVector(1.2f, 2.f, 1.4f));
	GrabZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GrabZone->SetCollisionObjectType(ECC_WorldDynamic);
	GrabZone->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void AToyZipPulley::Init(AToyZipline* InLine)
{
	Line = InLine;
	PaintZipPart(Wheel, ZipPulleyColor, this);
	PaintZipPart(Rod, ZipPostColor, this);
	PaintZipPart(Bar, ZipPulleyColor, this);
	// 横杆和绳子垂直：整个把手朝着绳子的水平方向
	SetActorRotation(FRotator(0.f, InLine->GetDirection().Rotation().Yaw, 0.f));
	SetDistance(0.f);
}

void AToyZipPulley::SetDistance(float InDistance)
{
	if (AToyZipline* OnLine = Line.Get())
	{
		Distance = FMath::Clamp(InDistance, 0.f, OnLine->GetLength());
		SetActorLocation(OnLine->PointAt(Distance));
	}
}

FVector AToyZipPulley::GetGripLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, ZipHandleDrop);
}

bool AToyZipPulley::CanGrab() const
{
	// 只有小身体能吊上去
	return ToyLilliput::GetScale() < 1.f && !Holder.IsValid();
}

void AToyZipPulley::OnGrabbed(AToyHand* Hand)
{
	Holder = Hand;
	if (AToyBoxPawn* Pawn = ZipPawn(GetWorld()))
	{
		Pawn->StartZipRide(Line.Get(), Hand);
	}
}

void AToyZipPulley::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	Holder.Reset();
	// 半路松手：人带着速度掉下去（滑到底时是玩家那边先停、再让手松开，这里再调一次也没关系）
	if (AToyBoxPawn* Pawn = ZipPawn(GetWorld()); Pawn && Pawn->IsZipRiding(Line.Get()))
	{
		Pawn->StopZipRide(false);
	}
}

void AToyZipPulley::AddHover()
{
	if (HoverCount++ == 0)
	{
		PaintZipPart(Bar, ZipHoverColor, this);
	}
}

void AToyZipPulley::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	if (HoverCount == 0)
	{
		PaintZipPart(Bar, ZipPulleyColor, this);
	}
}

bool AToyZipPulley::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = FText::FromString(TEXT("滑索"));
	OutBody = FText::FromString(TEXT("捏住就往下滑，松开就掉下去"));
	return true;
}

// ---------------------------------------------------------------- 线轴

AToyZipSpool::AToyZipSpool()
{
	PrimaryActorTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderFinder.Object);
	}
	// 4cm 粗、3cm 高的线轴（根组件的缩放：SpawnActor 时会乘到生成变换上）
	Mesh->SetRelativeScale3D(FVector(4.f, 4.f, 3.f) / 100.f);
	PreviewRope = MakeZipPart(this, TEXT("PreviewRope"), Mesh);
	PinMarker = MakeZipPart(this, TEXT("PinMarker"), Mesh);
	for (UStaticMeshComponent* Part : { PreviewRope.Get(), PinMarker.Get() })
	{
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetVisibility(false);
	}
	DisplayName = TEXT("滑索线轴");
	MaterialText = TEXT("塑料");
	MassKg = 0.15f;
}

void AToyZipSpool::BeginPlay()
{
	Super::BeginPlay();
	PaintZipPart(Mesh, ZipSpoolColor, this);
	PaintZipPart(PreviewRope, ZipRopeColor, this);
	PaintZipPart(PinMarker, ZipPostColor, this);
}

AToyZipSpool* AToyZipSpool::SpawnInFrontOfPlayer(UWorld* World)
{
	const APlayerCameraManager* Camera = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
	if (!Camera)
	{
		return nullptr;
	}
	const FRotator Yaw(0.f, Camera->GetCameraRotation().Yaw, 0.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToyZipSpool* Spool = World->SpawnActor<AToyZipSpool>(StaticClass(),
		FTransform(Yaw, Camera->GetCameraLocation() + Yaw.RotateVector(FVector(30.f, -20.f, -22.f))), Params);
	if (Spool)
	{
		// 悬着、亮着，等第一次捏起来
		Spool->bFloating = true;
		Spool->Mesh->SetSimulatePhysics(false);
		Spool->AddHover();
	}
	return Spool;
}

AToyZipSpool* AToyZipSpool::SpawnOnWorkbench(UWorld* World)
{
	AToyStudio* Studio = World ? AToyStudio::Find(World) : nullptr;
	if (!Studio)
	{
		return nullptr;
	}
	// 工作台面靠前、小人偶旁边的空地（工作室坐标），往下找台面
	const FVector From = Studio->GetActorTransform().TransformPosition(FVector(38.f, 36.f, 250.f));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ToyZipSpoolPlace), false);
	IgnoreZipActors(World, Query);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Query) || Hit.ImpactPoint.Z - Studio->GetActorLocation().Z < 50.f)
	{
		return nullptr;   // 没有工作台（没生成新版样板间）就不摆
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToyZipSpool* Spool = World->SpawnActor<AToyZipSpool>(StaticClass(),
		FTransform(Studio->GetActorRotation(), Hit.ImpactPoint + FVector(0.f, 0.f, 1.6f)), Params);
	if (Spool)
	{
		Spool->bReturnHome = true;
		UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：线轴摆在工作台上 %s"), *Spool->GetActorLocation().ToCompactString());
	}
	return Spool;
}

void AToyZipSpool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bHasPin)
	{
		// 第一头钉住以后：绳子从第一头连到线轴
		const FVector From = AToyZipline::AttachPoint(PinPoint, PinNormal);
		PlaceZipStick(PreviewRope, From, GetActorLocation(), ZipRopeDiameter);
	}
}

bool AToyZipSpool::FindSurface(FVector& OutPoint, FVector& OutNormal) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyZipSpoolSurface), false, this);
	IgnoreZipActors(GetWorld(), Params);
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);   // 家具、墙、地板（摆件会动，不能当钉点）
	const FVector Center = GetActorLocation();
	float Best = TNumericLimits<float>::Max();
	for (const FVector& Dir : { FVector::UpVector, FVector::DownVector, FVector::ForwardVector, FVector::BackwardVector, FVector::RightVector, FVector::LeftVector })
	{
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Center, Center + Dir * ZipSpoolReach, Objects, Params) && Hit.Distance < Best)
		{
			Best = Hit.Distance;
			OutPoint = Hit.ImpactPoint;
			OutNormal = Hit.ImpactNormal;
		}
	}
	return Best < TNumericLimits<float>::Max();
}

void AToyZipSpool::ShowToastToPlayer(const FString& Title, const FString& Body) const
{
	if (AToyBoxPawn* Pawn = ZipPawn(GetWorld()))
	{
		Pawn->ShowPlayerToast(Title, Body);
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox 滑索：%s / %s"), *Title, *Body);
}

void AToyZipSpool::OnGrabbed(AToyHand* Hand)
{
	if (bFloating)
	{
		bFloating = false;
		RemoveHover();
	}
	Super::OnGrabbed(Hand);
}

void AToyZipSpool::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	Super::OnReleased(Hand, Velocity);
	FVector Point, Normal;
	if (!FindSurface(Point, Normal))
	{
		return;   // 没贴着表面：普通地掉下去，第一头（有的话）还留着
	}
	if (!bHasPin)
	{
		// 钉住第一头：线轴留在那里（冻住）当桩
		bHasPin = true;
		PinPoint = Point;
		PinNormal = Normal;
		Mesh->SetSimulatePhysics(false);
		SetActorLocation(Point + Normal * 2.f, false, nullptr, ETeleportType::ResetPhysics);
		PlaceZipStick(PinMarker, Point, AToyZipline::AttachPoint(Point, Normal), ZipPostDiameter);
		PinMarker->SetVisibility(true);
		PreviewRope->SetVisibility(true);
		ShowToastToPlayer(TEXT("滑索：第一头钉好了"), TEXT("把线轴拿到另一个地方，贴着表面松手"));
		return;
	}
	FString Reason;
	if (AToyZipline::Create(GetWorld(), PinPoint, PinNormal, Point, Normal, Reason))
	{
		ShowToastToPlayer(TEXT("滑索拉好了"), TEXT("变小（左手 Y），走到高的那头，捏住把手往下滑"));
		Finish();
		return;
	}
	ShowToastToPlayer(TEXT("这里拉不成"), Reason);
}

void AToyZipSpool::Finish()
{
	bHasPin = false;
	PreviewRope->SetVisibility(false);
	PinMarker->SetVisibility(false);
	if (bReturnHome)
	{
		ResetToHome();
	}
	else
	{
		Destroy();
	}
}

bool AToyZipSpool::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = FText::FromString(DisplayName);
	OutBody = FText::FromString(bHasPin ? TEXT("贴着另一个表面松手，拉成滑索") : TEXT("贴着一个表面松手，钉住第一头"));
	return true;
}
