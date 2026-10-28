#include "ToyProp.h"

#include "ToyHand.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// 手靠近时的高亮：和零件一样，用虚影材质（M_ToyGhost）做一层半透明发光的叠加材质
	const TCHAR* GhostMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float EmissiveBoost = 12.f;

	// 粗估质量用的平均密度（千克 / 立方厘米）：摆件大多空心，按实心水的 1/4 算，再限制在 0.05~3kg
	constexpr float RoughDensity = 0.00025f;

	// 最长边小于这个（厘米）就用捏，否则整手握
	constexpr float PinchMaxCm = 12.f;
}

AToyProp::AToyProp()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Mesh->SetSimulatePhysics(true);
	// 开局睡着：叠在一起的书、挨着的瓶子不会一开场就互相顶开，被手碰到或别的东西撞到才醒
	Mesh->BodyInstance.bStartAwake = false;
	// 扔得快的时候防止穿过桌面和地板
	Mesh->BodyInstance.bUseCCD = true;
	SetRootComponent(Mesh);
}

void AToyProp::BeginPlay()
{
	Super::BeginPlay();
	HomeTransform = GetActorTransform();

	const FVector Size = Mesh->Bounds.BoxExtent * 2.f;
	const float Mass = MassKg > 0.f ? MassKg : FMath::Clamp(static_cast<float>(Size.X * Size.Y * Size.Z) * RoughDensity, 0.05f, 3.f);
	Mesh->SetMassOverrideInKg(NAME_None, Mass, true);

	if (UMaterialInterface* Ghost = LoadObject<UMaterialInterface>(nullptr, GhostMaterialPath))
	{
		HoverMaterial = UMaterialInstanceDynamic::Create(Ghost, this);
		HoverMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f) * EmissiveBoost);
		HoverMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
	}
	if (!Mesh->GetStaticMesh() || !Mesh->GetBodySetup())
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 摆件 %s 没有网格或碰撞，拿不起来"), *GetName());
	}
}

void AToyProp::ResetToHome()
{
	if (Holder.IsValid())
	{
		return;
	}
	Mesh->SetSimulatePhysics(false);
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::ResetPhysics);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Mesh->PutRigidBodyToSleep();
}

bool AToyProp::IsAwayFromHome() const
{
	const FTransform Now = GetActorTransform();
	return Holder.IsValid() || FVector::Dist(Now.GetLocation(), HomeTransform.GetLocation()) > 2.f
		|| Now.GetRotation().AngularDistance(HomeTransform.GetRotation()) > FMath::DegreesToRadians(5.f);
}

FTransform AToyProp::ComputeHoldTransform(const FToyGripContext& Grip) const
{
	// 自由捏取：保持捏起那一刻摆件相对手的位置和角度
	return FTransform(GetActorQuat(), GetActorLocation()).GetRelativeTransform(Grip.GripWorld);
}

void AToyProp::OnGrabbed(AToyHand* Hand)
{
	Holder = Hand;
	// 物理模拟中的刚体不跟随父组件，必须先关掉才能挂到手上
	Mesh->SetSimulatePhysics(false);
}

void AToyProp::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	Holder.Reset();
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeRigidBody();
	Mesh->SetPhysicsLinearVelocity(Velocity);
}

AToyHand* AToyProp::GetHolder() const
{
	return Holder.Get();
}

void AToyProp::AddHover()
{
	++HoverCount;
	RefreshOverlay();
}

void AToyProp::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	RefreshOverlay();
}

void AToyProp::RefreshOverlay()
{
	Mesh->SetOverlayMaterial(HoverCount > 0 ? HoverMaterial.Get() : nullptr);
}

EToyGripStyle AToyProp::GetGripStyle() const
{
	return Mesh->Bounds.BoxExtent.GetMax() * 2.f < PinchMaxCm ? EToyGripStyle::Pinch : EToyGripStyle::Power;
}

bool AToyProp::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	if (DisplayName.IsEmpty())
	{
		return false;
	}
	OutTitle = FText::FromString(DisplayName);
	OutBody = FText::FromString(MaterialText.IsEmpty() ? FString() : FString::Printf(TEXT("材料：%s"), *MaterialText));
	return true;
}
