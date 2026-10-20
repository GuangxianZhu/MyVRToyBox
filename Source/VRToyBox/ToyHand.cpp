#include "ToyHand.h"

#include "ToyBoxPawn.h"
#include "ToyCube.h"
#include "ToyShapeComponent.h"
#include "ToyTouchScreen.h"
#include "Components/ArrowComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

namespace
{
	// 右手尺寸，单位厘米。手感不对时先改这里。
	constexpr float PalmY = 3.f;          // 手掌中心在握把姿态右侧 3cm（握住手柄时手掌包在外侧）
	constexpr float PalmThickness = 2.5f;
	constexpr float KnuckleX = 4.5f;
	constexpr float FingerZ[4] = { 2.9f, 1.0f, -0.9f, -2.8f };      // 食指、中指、无名指、小指
	constexpr float FingerLength[4] = { 7.5f, 8.0f, 7.5f, 6.0f };
	constexpr float FingerWidth[4] = { 1.8f, 1.8f, 1.7f, 1.5f };
	const FRotator ThumbOpen(15.f, -30.f, 0.f);
	const FRotator AlongX(-90.f, 0.f, 0.f);  // 把圆柱的 Z 轴转到 +X
	const FLinearColor HandColor(0.55f, 0.75f, 1.f);
	const FLinearColor TipColor(1.f, 0.55f, 0.1f);
}

AToyHand::AToyHand()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(Root);

	auto Part = [this](FName Name, USceneComponent* Parent, EToyShape Shape, const FVector& Size, const FVector& Location,
		const FRotator& Rotation, const FLinearColor& Color)
	{
		UToyShapeComponent* Shape3D = UToyShapeComponent::Create(this, Name, Parent, Shape, Size, Location, Color, Rotation);
		// 手是"幽灵"：不推开方块，也不挡桌面模式的鼠标射线
		Shape3D->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		return Shape3D;
	};
	auto Joint = [this](FName Name, USceneComponent* Parent, const FVector& Location, const FRotator& Rotation)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(Name);
		Pivot->SetupAttachment(Parent);
		Pivot->SetRelativeLocationAndRotation(Location, Rotation);
		return Pivot;
	};

	Part(TEXT("Palm"), VisualRoot, EToyShape::Box, FVector(9.f, PalmThickness, 8.f), FVector(0.f, PalmY, 0.f), FRotator::ZeroRotator, HandColor);
	Part(TEXT("Wrist"), VisualRoot, EToyShape::Cylinder, FVector(4.5f, 4.5f, 6.f), FVector(-7.f, PalmY, 0.f), AlongX, HandColor);

	for (int32 Finger = 0; Finger < 4; ++Finger)
	{
		const float Proximal = FingerLength[Finger] * 0.55f;
		const float Distal = FingerLength[Finger] * 0.45f;
		const float Width = FingerWidth[Finger];

		USceneComponent* Base = Joint(*FString::Printf(TEXT("Finger%d_Base"), Finger), VisualRoot,
			FVector(KnuckleX, PalmY, FingerZ[Finger]), FRotator::ZeroRotator);
		Part(*FString::Printf(TEXT("Finger%d_Proximal"), Finger), Base, EToyShape::Cylinder,
			FVector(Width, Width, Proximal), FVector(Proximal * 0.5f, 0.f, 0.f), AlongX, HandColor);

		USceneComponent* Mid = Joint(*FString::Printf(TEXT("Finger%d_Mid"), Finger), Base,
			FVector(Proximal, 0.f, 0.f), FRotator::ZeroRotator);
		Part(*FString::Printf(TEXT("Finger%d_Distal"), Finger), Mid, EToyShape::Cylinder,
			FVector(Width, Width, Distal), FVector(Distal * 0.5f, 0.f, 0.f), AlongX, HandColor);

		FingerJoints.Add(Base);
		FingerJoints.Add(Mid);

		if (Finger == 0)
		{
			PokeTip = Joint(TEXT("PokeTip"), Mid, FVector(Distal, 0.f, 0.f), FRotator::ZeroRotator);
			Part(TEXT("PokeTipVisual"), PokeTip, EToyShape::Sphere, FVector(1.6f), FVector::ZeroVector, FRotator::ZeroRotator, TipColor);
		}
	}

	ThumbBase = Joint(TEXT("Thumb_Base"), VisualRoot, FVector(0.5f, PalmY - 1.2f, 3.6f), ThumbOpen);
	Part(TEXT("Thumb_Proximal"), ThumbBase, EToyShape::Cylinder, FVector(2.f, 2.f, 3.8f), FVector(1.9f, 0.f, 0.f), AlongX, HandColor);
	ThumbMid = Joint(TEXT("Thumb_Mid"), ThumbBase, FVector(3.8f, 0.f, 0.f), FRotator::ZeroRotator);
	Part(TEXT("Thumb_Distal"), ThumbMid, EToyShape::Cylinder, FVector(1.9f, 1.9f, 3.f), FVector(1.5f, 0.f, 0.f), AlongX, HandColor);

	// GripPoint 不放在 VisualRoot 下面：镜像（负缩放）会让它的旋转变得不可靠，所以单独挂在 Root 上，由 InitHand 按左右手摆好
	GripPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("GripPoint"));
	GripPoint->SetupAttachment(Root);
	GripPoint->ArrowColor = FColor(0, 220, 255);
	GripPoint->ArrowSize = 0.1f;
	GripPoint->SetHiddenInGame(true);
}

void AToyHand::InitHand(AToyBoxPawn* InPawn, EControllerHand InSide)
{
	OwnerPawn = InPawn;
	Side = InSide;

	const float Mirror = (Side == EControllerHand::Left) ? -1.f : 1.f;
	VisualRoot->SetRelativeScale3D(FVector(1.f, Mirror, 1.f));

	// 掌心表面在 Y = PalmY - 厚度/2 处；X 轴从掌心指向手背（右手 +Y，左手 -Y），Z 轴沿手指（+X）
	const FVector PalmSurface(1.f, Mirror * (PalmY - PalmThickness * 0.5f), 0.f);
	const FRotator GripRotation = FRotationMatrix::MakeFromXZ(FVector(0.f, Mirror, 0.f), FVector::ForwardVector).Rotator();
	GripPoint->SetRelativeLocationAndRotation(PalmSurface, GripRotation);
}

void AToyHand::SetInputs(float InGrip, float InTrigger)
{
	Grip = FMath::Clamp(InGrip, 0.f, 1.f);
	Trigger = FMath::Clamp(InTrigger, 0.f, 1.f);
}

void AToyHand::SetDesktopPose(const FVector& GripWorldLocation, const FRotator& Facing)
{
	const FQuat Rotation = Facing.Quaternion();
	SetActorLocationAndRotation(GripWorldLocation - Rotation.RotateVector(GripPoint->GetRelativeLocation()), Rotation);
}

void AToyHand::SetPokeEnabled(bool bEnabled)
{
	bPokeEnabled = bEnabled;
	if (!bEnabled)
	{
		Touching.Reset();
	}
}

bool AToyHand::IsHolding() const
{
	return HeldCube.IsValid();
}

AToyCube* AToyHand::GetHeldCube() const
{
	return HeldCube.Get();
}

void AToyHand::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RecordMotion();

	// 滞回阈值：按过 0.6 才算抓，松到 0.35 以下才算放，避免握把在中间值抖动时反复抓放
	if (!bGripDown && Grip > 0.6f)
	{
		bGripDown = true;
		if (AToyCube* Candidate = FindGrabCandidate())
		{
			Grab(Candidate);
		}
	}
	else if (bGripDown && Grip < 0.35f)
	{
		bGripDown = false;
		ReleaseHeld(true);
	}

	UpdateHover();
	TickHeld(DeltaSeconds);
	TickCurls(DeltaSeconds);
	if (bPokeEnabled)
	{
		TickPoke();
	}

	GripPoint->SetHiddenInGame(!ToyBox::ShowGrabPoints());
}

AToyCube* AToyHand::FindGrabCandidate() const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyHandGrab), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, GripPoint->GetComponentLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_PhysicsBody), FCollisionShape::MakeSphere(GrabRadius), Params);

	AToyCube* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AToyCube* Cube = Cast<AToyCube>(Overlap.GetActor());
		if (!Cube || Cube == HeldCube.Get())
		{
			continue;
		}
		const float Distance = FVector::DistSquared(Cube->GetActorLocation(), GripPoint->GetComponentLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Cube;
		}
	}
	return Best;
}

void AToyHand::Grab(AToyCube* Cube)
{
	// 另一只手正拿着它：直接交接，不给速度
	if (AToyHand* Other = Cube->GetHolder())
	{
		Other->ReleaseHeld(false);
	}

	// 先用世界坐标算好目标，再挂上去；挂的时候保持世界位置不变，然后在 SnapTime 内滑到目标
	const FTransform Hold = Cube->ComputeHoldTransform(GripPoint->GetComponentTransform());
	Cube->OnGrabbed(this);
	Cube->AttachToComponent(GripPoint, FAttachmentTransformRules::KeepWorldTransform);

	SnapFrom = Cube->GetRootComponent()->GetRelativeTransform();
	SnapTo = Hold;
	SnapTo.SetScale3D(SnapFrom.GetScale3D());
	SnapAlpha = SnapTime > 0.f ? 0.f : 1.f;
	if (SnapAlpha >= 1.f)
	{
		Cube->GetRootComponent()->SetRelativeTransform(SnapTo);
	}

	HeldCube = Cube;
	Pulse(0.7f, 0.08f);
}

void AToyHand::ReleaseHeld(bool bThrow)
{
	AToyCube* Cube = HeldCube.Get();
	HeldCube.Reset();
	if (!Cube)
	{
		return;
	}
	Cube->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Cube->OnReleased(bThrow ? GetThrowVelocity() : FVector::ZeroVector);
}

void AToyHand::UpdateHover()
{
	AToyCube* Candidate = IsHolding() ? nullptr : FindGrabCandidate();
	if (Candidate == HoveredCube.Get())
	{
		return;
	}
	if (AToyCube* Previous = HoveredCube.Get())
	{
		Previous->RemoveHover();
	}
	HoveredCube = Candidate;
	if (Candidate)
	{
		Candidate->AddHover();
	}
}

void AToyHand::TickHeld(float DeltaSeconds)
{
	AToyCube* Cube = HeldCube.Get();
	if (!Cube || SnapAlpha >= 1.f)
	{
		return;
	}
	SnapAlpha = FMath::Min(1.f, SnapAlpha + DeltaSeconds / SnapTime);
	FTransform Blended;
	Blended.Blend(SnapFrom, SnapTo, FMath::InterpEaseOut(0.f, 1.f, SnapAlpha, 2.f));
	Cube->GetRootComponent()->SetRelativeTransform(Blended);
}

void AToyHand::TickCurls(float DeltaSeconds)
{
	const float MinCurl = IsHolding() ? HoldCurl : 0.f;
	FingerCurl = FMath::FInterpTo(FingerCurl, FMath::Max(Grip * MaxCurl, MinCurl), DeltaSeconds, 20.f);
	IndexCurl = FMath::FInterpTo(IndexCurl, FMath::Max(Trigger * MaxCurl, MinCurl), DeltaSeconds, 20.f);

	// 绕 Z 轴负方向转 = 手指朝掌心（-Y）弯；左手在镜像空间里自动变成反方向
	for (int32 Finger = 0; Finger < 4; ++Finger)
	{
		const float Curl = (Finger == 0) ? IndexCurl : FingerCurl;
		FingerJoints[Finger * 2]->SetRelativeRotation(FRotator(0.f, -Curl, 0.f));
		FingerJoints[Finger * 2 + 1]->SetRelativeRotation(FRotator(0.f, -Curl * 1.1f, 0.f));
	}
	ThumbBase->SetRelativeRotation(ThumbOpen + FRotator(-FingerCurl * 0.2f, -FingerCurl * 0.4f, 0.f));
	ThumbMid->SetRelativeRotation(FRotator(0.f, -FingerCurl * 0.6f, 0.f));
}

void AToyHand::TickPoke()
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyHandPoke), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, PokeTip->GetComponentLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(1.f), Params);

	// 只在"刚碰到"的那一帧触发，手指停在按钮里不会连发
	TArray<TWeakObjectPtr<UPrimitiveComponent>> NowTouching;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		AToyTouchScreen* Screen = Cast<AToyTouchScreen>(Overlap.GetActor());
		if (!Component || !Screen)
		{
			continue;
		}
		const TWeakObjectPtr<UPrimitiveComponent> WeakComponent(Component);
		NowTouching.Add(WeakComponent);
		if (!Touching.Contains(WeakComponent) && Screen->Poke(Component))
		{
			Pulse(0.5f, 0.05f);
		}
	}
	Touching = MoveTemp(NowTouching);
}

void AToyHand::RecordMotion()
{
	MotionSamples.Add(FMotionSample{ GripPoint->GetComponentLocation(), GetWorld()->GetTimeSeconds() });
	if (MotionSamples.Num() > 6)
	{
		MotionSamples.RemoveAt(0);
	}
}

FVector AToyHand::GetThrowVelocity() const
{
	// 用最近几帧的平均速度，比只看最后一帧稳定（单帧追踪抖动会让扔出去的方向乱飘）
	if (MotionSamples.Num() < 2)
	{
		return FVector::ZeroVector;
	}
	const FMotionSample& First = MotionSamples[0];
	const FMotionSample& Last = MotionSamples.Last();
	const double Elapsed = Last.Time - First.Time;
	if (Elapsed <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}
	return ((Last.Location - First.Location) / Elapsed).GetClampedToMaxSize(1200.f);
}

void AToyHand::Pulse(float Amplitude, float Duration) const
{
	if (AToyBoxPawn* Pawn = OwnerPawn.Get())
	{
		Pawn->PlayHaptic(Side, Amplitude, Duration);
	}
}

void AToyHand::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseHeld(false);
	if (AToyCube* Previous = HoveredCube.Get())
	{
		Previous->RemoveHover();
	}
	Super::EndPlay(EndPlayReason);
}
