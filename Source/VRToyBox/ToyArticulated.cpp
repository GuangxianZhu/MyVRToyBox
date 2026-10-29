#include "ToyArticulated.h"

#include "ToyHand.h"
#include "ToyStudio.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

// 常量名带 Articulated 前缀：unity 合并编译时别的 .cpp 匿名命名空间里有同名常量（GhostMaterialPath 这些）
namespace ToyArticulatedLocal
{
	// 手靠近时的高亮：和摆件一样的虚影叠加材质
	const TCHAR* ArticulatedGhostMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float ArticulatedEmissiveBoost = 12.f;
	/** 手离铰链轴不到这么多（厘米）时角度算不准，不跟 */
	constexpr float MinHingeRadius = 3.f;

	FAutoConsoleCommandWithWorldAndArgs OpenCommand(TEXT("toy.Open"),
		TEXT("把标签里带 <文字> 的柜门 / 抽屉开到 <开度>（柜门是度、抽屉是厘米，0 = 关上）：toy.Open <文字> <开度>。截图、测试用"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 2)
			{
				return;
			}
			for (TActorIterator<AToyArticulated> It(World); It; ++It)
			{
				if (It->GetActorNameOrLabel().Contains(Args[0]))
				{
					It->SetValue(FCString::Atof(*Args[1]));
					UE_LOG(LogTemp, Display, TEXT("ToyBox: %s 开到 %.0f"), *It->GetActorNameOrLabel(), It->GetValue());
				}
			}
		}));
}

using namespace ToyArticulatedLocal;

AToyArticulated::AToyArticulated()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	// WorldDynamic：手的抓取检测查 PhysicsBody 和 WorldDynamic；挡人、挡摆件，自己不做物理
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetSimulatePhysics(false);
	SetRootComponent(Mesh);
}

void AToyArticulated::BeginPlay()
{
	Super::BeginPlay();
	HomeTransform = GetActorTransform();
	Axis = Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	if (UMaterialInterface* Ghost = LoadObject<UMaterialInterface>(nullptr, ArticulatedGhostMaterialPath))
	{
		HoverMaterial = UMaterialInstanceDynamic::Create(Ghost, this);
		float Brightness = 1.f;
		for (TActorIterator<AToyStudio> It(GetWorld()); It; ++It)
		{
			Brightness = It->UiBrightnessScale;
		}
		HoverMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f) * ArticulatedEmissiveBoost * Brightness);
		HoverMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
	}
}

void AToyArticulated::SetValue(float NewValue)
{
	Value = FMath::Clamp(NewValue, 0.f, MaxValue);
	// 关上时的变换上叠一个"绕铰链转 / 沿轴平移"（Actor 坐标系里）
	FTransform Local = FTransform::Identity;
	if (JointType == EToyJointType::Hinge)
	{
		const FQuat Turn(Axis, FMath::DegreesToRadians(Value));
		Local = FTransform(Turn, Pivot - Turn.RotateVector(Pivot));
	}
	else
	{
		Local.SetTranslation(Axis * Value);
	}
	SetActorTransform(Local * HomeTransform);
}

void AToyArticulated::ResetToHome()
{
	if (!Holder.IsValid())
	{
		SetValue(0.f);
	}
}

float AToyArticulated::HandParameter(const FVector& HandLocation) const
{
	const FVector WorldAxis = HomeTransform.TransformVectorNoScale(Axis);
	if (JointType == EToyJointType::Slide)
	{
		return static_cast<float>(FVector::DotProduct(HandLocation - HomeTransform.GetLocation(), WorldAxis));
	}
	// 铰链：手相对轴的角度，以"关上时轴指向门板中心"为 0
	const FVector WorldPivot = HomeTransform.TransformPosition(Pivot);
	const FVector Center = HomeTransform.TransformPosition(Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetBounds().Origin : FVector::ZeroVector);
	const FVector RefA = FVector::VectorPlaneProject(Center - WorldPivot, WorldAxis).GetSafeNormal();
	const FVector RefB = FVector::CrossProduct(WorldAxis, RefA);
	const FVector ToHand = FVector::VectorPlaneProject(HandLocation - WorldPivot, WorldAxis);
	if (ToHand.Size() < MinHingeRadius)
	{
		return TNumericLimits<float>::Max();
	}
	return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(ToHand, RefB), FVector::DotProduct(ToHand, RefA)));
}

void AToyArticulated::UpdateConstrainedGrab(const FVector& HandLocation)
{
	const float Now = HandParameter(HandLocation);
	if (Now == TNumericLimits<float>::Max() || HandAtGrab == TNumericLimits<float>::Max())
	{
		return;
	}
	// 角度跨过 ±180° 时接上
	SetValue(ValueAtGrab + (JointType == EToyJointType::Hinge ? FMath::FindDeltaAngleDegrees(HandAtGrab, Now) : Now - HandAtGrab));
}

FTransform AToyArticulated::ComputeHoldTransform(const FToyGripContext& Grip) const
{
	// 不挂到手上（IsConstrained），这个只是接口要求
	return GetActorTransform().GetRelativeTransform(Grip.GripWorld);
}

void AToyArticulated::OnGrabbed(AToyHand* Hand)
{
	Holder = Hand;
	ValueAtGrab = Value;
	HandAtGrab = Hand ? HandParameter(Hand->GetPinchLocation()) : 0.f;
}

void AToyArticulated::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	// 停在松手的地方
	Holder.Reset();
}

AToyHand* AToyArticulated::GetHolder() const
{
	return Holder.Get();
}

void AToyArticulated::AddHover()
{
	++HoverCount;
	RefreshOverlay();
}

void AToyArticulated::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	RefreshOverlay();
}

void AToyArticulated::RefreshOverlay()
{
	Mesh->SetOverlayMaterial(HoverCount > 0 ? HoverMaterial.Get() : nullptr);
}

bool AToyArticulated::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = FText::FromString(DisplayName);
	OutBody = FText::FromString(JointType == EToyJointType::Hinge
		? FString::Printf(TEXT("开了 %.0f°（最多 %.0f°）"), Value, MaxValue)
		: FString::Printf(TEXT("拉出 %.0fcm（最多 %.0fcm）"), Value, MaxValue));
	return true;
}
