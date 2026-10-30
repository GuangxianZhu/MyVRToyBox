#include "ToyLilliput.h"

#include "ToyShapeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// 常量名带 Lilli 前缀：unity 合并编译时别的 .cpp 匿名命名空间里的同名常量会二义
namespace ToyLilliputLocal
{
	TAutoConsoleVariable<int32> CVarLilliput(
		TEXT("toy.Lilliput"),
		1,
		TEXT("小人国模式：1 = 开（不摆桌子和爆炸图模型，左手柄 Y / 桌面 G 在大身体和小人偶之间切换），0 = 原来的桌子 + 爆炸图玩法。下次开局生效"));

	float GLilliScale = 1.f;

	/** UE 默认的近裁剪面（厘米）。 */
	constexpr float LilliDefaultNearClip = 10.f;

	// 小人偶：身体圆柱 5cm 粗、10cm 高，头 4cm（中心在身体中心上方 7cm），总高 14cm
	constexpr float LilliFigBodyDiameter = 5.f;
	constexpr float LilliFigBodyHeight = 10.f;
	constexpr float LilliFigHeadDiameter = 4.f;
	constexpr float LilliFigHeadAbove = 7.f;
	const FLinearColor LilliBodyColor(1.f, 0.32f, 0.05f);
	const FLinearColor LilliSkinColor(0.95f, 0.75f, 0.6f);

	// 大人像：身体 40cm 粗，头 24cm，头中心比眼睛高 4cm
	constexpr float LilliGiantBodyDiameter = 40.f;
	constexpr float LilliGiantHeadDiameter = 24.f;
}

using namespace ToyLilliputLocal;

bool ToyLilliput::IsEnabled()
{
	return CVarLilliput.GetValueOnGameThread() != 0;
}

float ToyLilliput::GetScale()
{
	return GLilliScale;
}

void ToyLilliput::SetScale(UWorld* World, float Scale)
{
	GLilliScale = FMath::Clamp(Scale, 0.01f, 1.f);
	if (!World)
	{
		return;
	}
	if (AWorldSettings* Settings = World->GetWorldSettings())
	{
		Settings->WorldToMeters = 100.f * GLilliScale;
	}
	if (GEngine)
	{
		GEngine->Exec(World, *FString::Printf(TEXT("r.SetNearClipPlane %.3f"), LilliDefaultNearClip * GLilliScale));
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：比例 %.4f，WorldToMeters %.2f，近裁剪面 %.2fcm"),
		GLilliScale, 100.f * GLilliScale, LilliDefaultNearClip * GLilliScale);
}

AToyFigurine::AToyFigurine()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderFinder.Object);
	}
	// 基础圆柱是 100 单位、中心在原点
	Mesh->SetRelativeScale3D(FVector(LilliFigBodyDiameter, LilliFigBodyDiameter, LilliFigBodyHeight) / 100.f);

	// 头不跟着身体的（不等比）缩放走：用绝对缩放，位置是身体局部坐标（Z 缩了 0.1 倍）
	Head = UToyShapeComponent::Create(this, TEXT("Head"), Mesh, EToyShape::Sphere, FVector(LilliFigHeadDiameter),
		FVector(0.f, 0.f, LilliFigHeadAbove / (LilliFigBodyHeight / 100.f)), LilliSkinColor, FRotator::ZeroRotator);
	Head->SetUsingAbsoluteScale(true);
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Head->SetCastShadow(true);

	DisplayName = TEXT("小人偶");
	MaterialText = TEXT("塑料");
	MassKg = 0.08f;
}

void AToyFigurine::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Color = UMaterialInstanceDynamic::Create(Base, this);
		Color->SetVectorParameterValue(TEXT("Color"), LilliBodyColor);
		Mesh->SetMaterial(0, Color);
	}
	// 小人偶也得在醒着时才会立住：刚生成时离地面一点点，叫醒让它落稳
	Mesh->WakeRigidBody();
}

AToyFigurine* AToyFigurine::Find(const UWorld* World)
{
	for (TActorIterator<AToyFigurine> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AToyFigurine::Hide()
{
	Mesh->SetSimulatePhysics(false);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AToyFigurine::StandAt(const FVector& FeetLocation, float Yaw)
{
	// 身体中心比脚高半个身高，再抬 0.3cm 免得一出现就嵌在地面里
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorLocationAndRotation(FeetLocation + FVector(0.f, 0.f, LilliFigBodyHeight * 0.5f + 0.3f), FRotator(0.f, Yaw, 0.f),
		false, nullptr, ETeleportType::ResetPhysics);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Mesh->WakeRigidBody();
}

FVector AToyFigurine::GetFeetLocation() const
{
	const FBoxSphereBounds Bounds = Mesh->Bounds;
	const FVector Center = Bounds.Origin;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyFigurineFeet), false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Center, Center - FVector(0.f, 0.f, Bounds.BoxExtent.Z + 20.f), ECC_Visibility, Params))
	{
		return Hit.ImpactPoint;
	}
	return Center - FVector(0.f, 0.f, Bounds.BoxExtent.Z);
}

AToyGiantBody::AToyGiantBody()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	// 尺寸、位置在 ShowAt 里按眼睛高度重新摆
	Body = UToyShapeComponent::Create(this, TEXT("Body"), Root, EToyShape::Cylinder,
		FVector(LilliGiantBodyDiameter, LilliGiantBodyDiameter, 140.f), FVector(0.f, 0.f, 70.f), LilliBodyColor, FRotator::ZeroRotator);
	Head = UToyShapeComponent::Create(this, TEXT("Head"), Root, EToyShape::Sphere,
		FVector(LilliGiantHeadDiameter), FVector(0.f, 0.f, 164.f), LilliSkinColor, FRotator::ZeroRotator);
	Nose = UToyShapeComponent::Create(this, TEXT("Nose"), Root, EToyShape::Box,
		FVector(6.f, 4.f, 4.f), FVector(LilliGiantHeadDiameter * 0.5f, 0.f, 160.f), LilliSkinColor, FRotator::ZeroRotator);
	for (UToyShapeComponent* Part : { Body.Get(), Head.Get(), Nose.Get() })
	{
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	Nose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AToyGiantBody::ShowAt(const FVector& Feet, float Yaw, float EyeHeight)
{
	const float Eye = FMath::Clamp(EyeHeight, 60.f, 220.f);
	const float BodyHeight = Eye - LilliGiantHeadDiameter * 0.5f;
	Body->SetShape(EToyShape::Cylinder, FVector(LilliGiantBodyDiameter, LilliGiantBodyDiameter, BodyHeight));
	Body->SetRelativeLocation(FVector(0.f, 0.f, BodyHeight * 0.5f));
	Head->SetRelativeLocation(FVector(0.f, 0.f, Eye + 4.f));
	Nose->SetRelativeLocation(FVector(LilliGiantHeadDiameter * 0.5f, 0.f, Eye));
	SetActorLocationAndRotation(Feet, FRotator(0.f, Yaw, 0.f));
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AToyGiantBody::Hide()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}
