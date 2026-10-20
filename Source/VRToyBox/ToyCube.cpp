#include "ToyCube.h"

#include "ToyHand.h"
#include "ToyShapeComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/CollisionProfile.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarShowGrabPoints(
	TEXT("toy.ShowGrabPoints"),
	0,
	TEXT("1 = 在游戏里显示方块和手的抓点箭头（之后新生成的方块生效）"));

bool ToyBox::ShowGrabPoints()
{
	return CVarShowGrabPoints.GetValueOnGameThread() != 0;
}

AToyCube::AToyCube()
{
	Mesh = CreateDefaultSubobject<UToyShapeComponent>(TEXT("Mesh"));
	Mesh->SetShape(EToyShape::Box, FVector(SizeCm));
	Mesh->Color = BaseColor;
	Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetMassOverrideInKg(NAME_None, 0.4f, true);
	// 扔得快的时候防止穿过桌面
	Mesh->BodyInstance.bUseCCD = true;
	SetRootComponent(Mesh);

	const FVector Normals[6] = {
		FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
		FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f),
		FVector(0.f, 0.f, 1.f), FVector(0.f, 0.f, -1.f),
	};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FVector Normal = Normals[Index];
		const FVector Tangent = FMath::Abs(Normal.Z) < 0.5f ? FVector::UpVector : FVector::ForwardVector;

		UArrowComponent* Point = CreateDefaultSubobject<UArrowComponent>(*FString::Printf(TEXT("GrabPoint_%d"), Index));
		Point->SetupAttachment(Mesh);
		// Mesh 是 100 单位的立方体缩放到 0.1，所以面中心在本地坐标 ±50
		Point->SetRelativeLocationAndRotation(Normal * 50.f, FRotationMatrix::MakeFromXZ(Normal, Tangent).Rotator());
		Point->ArrowColor = FColor(255, 220, 0);
		Point->ArrowSize = 0.6f;
		Point->SetHiddenInGame(true);
		GrabPoints.Add(Point);
	}
}

void AToyCube::BeginPlay()
{
	Super::BeginPlay();
	for (UArrowComponent* Point : GrabPoints)
	{
		Point->SetHiddenInGame(!ToyBox::ShowGrabPoints());
	}
}

FTransform AToyCube::ComputeHoldTransform(const FTransform& GripWorld) const
{
	const FVector HandX = GripWorld.GetUnitAxis(EAxis::X);
	const FVector HandZ = GripWorld.GetUnitAxis(EAxis::Z);
	const FVector GripLocation = GripWorld.GetLocation();

	// 1) 选面：朝向越接近手掌越好，离手越近越好
	const UArrowComponent* Best = nullptr;
	float BestScore = TNumericLimits<float>::Lowest();
	for (const UArrowComponent* Point : GrabPoints)
	{
		const float Facing = FVector::DotProduct(Point->GetForwardVector(), HandX);
		const float Distance = FVector::Dist(Point->GetComponentLocation(), GripLocation) / SizeCm;
		const float Score = Facing - 0.5f * Distance;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Point;
		}
	}
	if (!Best)
	{
		return FTransform::Identity;
	}

	// 2) 方块是对称的：绕面法线有 4 种 90° 摆法，挑 Z 轴和手指方向最一致的，抓起来时方块转得最少
	const FQuat FaceRotation = Best->GetComponentQuat();
	FQuat BestRotation = FaceRotation;
	float BestAlignment = -2.f;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		const FQuat Candidate = FaceRotation * FQuat(FVector::XAxisVector, Step * UE_HALF_PI);
		const float Alignment = FVector::DotProduct(Candidate.GetAxisZ(), HandZ);
		if (Alignment > BestAlignment)
		{
			BestAlignment = Alignment;
			BestRotation = Candidate;
		}
	}

	// 3) 要让 抓点世界变换 == 手抓点世界变换：
	//    Grab = GrabInCube * Cube = Grip  =>  Cube = GrabInCube^-1 * Grip
	//    所以方块相对手抓点的变换就是 GrabInCube 的逆。缩放先剥掉，调用方再补回去。
	const FTransform CubeWorld(GetActorQuat(), GetActorLocation());
	const FTransform GrabWorld(BestRotation, Best->GetComponentLocation());
	return GrabWorld.GetRelativeTransform(CubeWorld).Inverse();
}

void AToyCube::OnGrabbed(AToyHand* Hand)
{
	Holder = Hand;
	// 物理模拟中的刚体不跟随父组件，必须先关掉才能挂到手上
	Mesh->SetSimulatePhysics(false);
}

void AToyCube::OnReleased(const FVector& Velocity)
{
	Holder.Reset();
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
}

AToyHand* AToyCube::GetHolder() const
{
	return Holder.Get();
}

void AToyCube::SetBaseColor(const FLinearColor& InColor)
{
	BaseColor = InColor;
	RefreshColor();
}

void AToyCube::AddHover()
{
	++HoverCount;
	RefreshColor();
}

void AToyCube::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	RefreshColor();
}

void AToyCube::RefreshColor()
{
	Mesh->SetColor(HoverCount > 0 ? BaseColor + (FLinearColor::White - BaseColor) * 0.45f : BaseColor);
}
