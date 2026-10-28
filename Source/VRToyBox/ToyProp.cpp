#include "ToyProp.h"

#include "ToyHand.h"
#include "ToyStudio.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace ToyPropLocal
{
	// 手靠近时的高亮：和零件一样，用虚影材质（M_ToyGhost）做一层半透明发光的叠加材质
	const TCHAR* GhostMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float EmissiveBoost = 12.f;

	// 粗估质量用的平均密度（千克 / 立方厘米）：摆件大多空心，按实心水的 1/4 算，再限制在 0.05~3kg
	constexpr float RoughDensity = 0.00025f;

	// 最长边小于这个（厘米）就用捏，否则整手握
	constexpr float PinchMaxCm = 12.f;

	constexpr float AngularDamping = 1.f;
	/** 卡在别的东西里松手时，往外挤的最大速度（厘米 / 秒） */
	constexpr float MaxDepenetrationSpeed = 40.f;
}

using namespace ToyPropLocal;

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
	// 摆件放回去要很快停稳：侧躺的陀螺、倒下的蜡烛不会来回晃好几秒
	Mesh->SetAngularDamping(AngularDamping);
	// 拿着的时候摆件不受碰撞（会伸进架子、桌面里），在里面松手时慢慢挤出来，不要一下弹飞几米
	Mesh->BodyInstance.SetMaxDepenetrationVelocity(MaxDepenetrationSpeed);
	SetRootComponent(Mesh);
}

void AToyProp::BeginPlay()
{
	Super::BeginPlay();
	HomeTransform = GetActorTransform();

	// 挂在上面的植物、花：注册时会被焊进摆件的刚体（碰撞形状、质量都算进去，花的凸包一大圈），拆开、关掉碰撞，只跟着走
	TArray<AActor*> Riders;
	GetAttachedActors(Riders, true, true);
	for (AActor* Rider : Riders)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Rider->GetComponents(Meshes);
		for (UStaticMeshComponent* RiderMesh : Meshes)
		{
			RiderMesh->UnWeldFromParent();
			RiderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Rider->SetActorEnableCollision(false);
		RiderMeshes.Append(Meshes);
	}

	const FVector Size = Mesh->Bounds.BoxExtent * 2.f;
	const float Mass = MassKg > 0.f ? MassKg : FMath::Clamp(static_cast<float>(Size.X * Size.Y * Size.Z) * RoughDensity, 0.05f, 3.f);
	Mesh->SetMassOverrideInKg(NAME_None, Mass, true);

	if (UMaterialInterface* Ghost = LoadObject<UMaterialInterface>(nullptr, GhostMaterialPath))
	{
		HoverMaterial = UMaterialInstanceDynamic::Create(Ghost, this);
		// 样板间是固定曝光、再减 1.5 档，发光的东西要和触屏、信息牌一样按工作室的倍数提亮，不然高亮几乎看不见
		float Brightness = 1.f;
		for (TActorIterator<AToyStudio> It(GetWorld()); It; ++It)
		{
			Brightness = It->UiBrightnessScale;
		}
		HoverMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f) * EmissiveBoost * Brightness);
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
	// 叠在它上面的摆件（书上的几何摆件）是睡着的，底下的东西被拿走也不会自己醒，会悬在半空：先叫醒
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyPropNeighbours), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Mesh->Bounds.Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody),
		FCollisionShape::MakeBox(Mesh->Bounds.BoxExtent + FVector(2.f)), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AToyProp* Other = Cast<AToyProp>(Overlap.GetActor()); Other && !Other->Holder.IsValid())
		{
			Other->Mesh->WakeRigidBody();
		}
	}
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
	UMaterialInterface* Overlay = HoverCount > 0 ? HoverMaterial.Get() : nullptr;
	Mesh->SetOverlayMaterial(Overlay);
	for (UStaticMeshComponent* Rider : RiderMeshes)
	{
		if (Rider)
		{
			Rider->SetOverlayMaterial(Overlay);
		}
	}
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
