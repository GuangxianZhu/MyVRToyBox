#include "ToyHand.h"

#include "ToyBoxPawn.h"
#include "ToyCube.h"
#include "ToyGrabbable.h"
#include "ToyShapeComponent.h"
#include "ToyTouchScreen.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ArrowComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

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

	// 信息牌：Slate 像素 → 厘米。560 像素宽 ≈ 22cm，手边看正合适
	constexpr float LabelScale = 0.04f;
	constexpr float LabelWidthPx = 560.f;
	constexpr float LabelBoost = 12.f;

	IToyGrabbable* AsGrabbable(AActor* Actor)
	{
		return Cast<IToyGrabbable>(Actor);
	}
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

	// 同理 PinchPoint 也挂在 Root 上，位置由 InitHand 镜像
	PinchPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PinchPoint"));
	PinchPoint->SetupAttachment(Root);

	// 信息牌用世界坐标摆放（跟着拿着的东西走、转向玩家），所以位置、旋转、缩放都不继承手
	InfoLabel = CreateDefaultSubobject<UWidgetComponent>(TEXT("InfoLabel"));
	InfoLabel->SetupAttachment(Root);
	InfoLabel->SetUsingAbsoluteLocation(true);
	InfoLabel->SetUsingAbsoluteRotation(true);
	InfoLabel->SetUsingAbsoluteScale(true);
	InfoLabel->SetWorldScale3D(FVector(LabelScale));
	InfoLabel->SetWidgetSpace(EWidgetSpace::World);
	InfoLabel->SetDrawAtDesiredSize(true);
	InfoLabel->SetPivot(FVector2D(0.5f, 1.f));
	InfoLabel->SetTwoSided(true);
	InfoLabel->SetBlendMode(EWidgetBlendMode::Transparent);
	InfoLabel->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	InfoLabel->SetCastShadow(false);
	InfoLabel->SetHiddenInGame(true);
	// 不受光照的 UI 在自动曝光下会显得很暗（颜色值 1 ≈ 1 尼特，房间是几十尼特），整体提亮
	InfoLabel->SetTintColorAndOpacity(FLinearColor(LabelBoost, LabelBoost, LabelBoost, 1.f));
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

	// 捏的位置：食指第二节和拇指尖之间，在掌心前方一点
	PinchPoint->SetRelativeLocation(FVector(8.5f, Mirror * -0.5f, 3.2f));

	// 信息牌：标题一行，下面材料和作用自动换行。字体用引擎默认字体，它带中文后备字形
	SAssignNew(InfoTitle, STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
		.ColorAndOpacity(FLinearColor::White)
		.AutoWrapText(true);
	SAssignNew(InfoBody, STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
		.ColorAndOpacity(FLinearColor(0.85f, 0.9f, 1.f))
		.AutoWrapText(true);
	InfoLabel->SetSlateWidget(
		SNew(SBox)
		.WidthOverride(LabelWidthPx)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.04f, 0.09f, 0.85f))
			.Padding(FMargin(18.f, 12.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[InfoTitle.ToSharedRef()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[InfoBody.ToSharedRef()]
			]
		]);
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
	return HeldActor.IsValid();
}

AActor* AToyHand::GetHeldActor() const
{
	return HeldActor.Get();
}

void AToyHand::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RecordMotion();

	// 滞回阈值：按过 0.6 才算抓，松到 0.35 以下才算放，避免握把在中间值抖动时反复抓放
	if (!bGripDown && Grip > 0.6f)
	{
		bGripDown = true;
		if (AActor* Candidate = FindGrabCandidate())
		{
			Grab(Candidate);
		}
	}
	else if (bGripDown && Grip < 0.35f)
	{
		bGripDown = false;
		ReleaseHeld(true);
	}

	// 拿着的东西被别处销毁了（比如收起了电机）：把信息牌也收掉
	if (!HeldActor.IsValid() && !InfoLabel->bHiddenInGame)
	{
		ShowInfo(nullptr);
	}

	UpdateHover();
	TickHeld(DeltaSeconds);
	TickCurls(DeltaSeconds);
	TickInfo();
	if (bPokeEnabled)
	{
		TickPoke();
	}

	GripPoint->SetHiddenInGame(!ToyBox::ShowGrabPoints());
}

AActor* AToyHand::FindGrabCandidate() const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyHandGrab), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);   // 方块
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);  // 电机零件 / 整机的检测盒
	const FVector GripLocation = GripPoint->GetComponentLocation();
	GetWorld()->OverlapMultiByObjectType(Overlaps, GripLocation, FQuat::Identity, Objects, FCollisionShape::MakeSphere(GrabRadius), Params);

	// 按"手到碰撞体表面的距离"挑最近的；都贴着（比如手伸进了两个零件的检测盒）时挑小的那个，
	// 这样螺钉、平键这种小件不会被旁边端盖、转子的大盒子抢走
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	float BestSize = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		const IToyGrabbable* Grabbable = AsGrabbable(Actor);
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Grabbable || !Component || Actor == HeldActor.Get() || !Grabbable->CanGrab())
		{
			continue;
		}
		if (Actor == PreferredTarget.Get())
		{
			return Actor;
		}
		FVector Closest;
		float Distance = Component->GetClosestPointOnCollision(GripLocation, Closest);
		if (Distance < 0.f)
		{
			Distance = FVector::Dist(Component->Bounds.Origin, GripLocation);
		}
		const float Size = Component->Bounds.SphereRadius;
		const bool bCloser = Distance < BestDistance - 0.5f;
		const bool bTieButSmaller = FMath::Abs(Distance - BestDistance) <= 0.5f && Size < BestSize;
		if (bCloser || bTieButSmaller)
		{
			BestDistance = Distance;
			BestSize = Size;
			Best = Actor;
		}
	}
	return Best;
}

void AToyHand::Grab(AActor* Target)
{
	IToyGrabbable* Grabbable = AsGrabbable(Target);
	if (!Grabbable)
	{
		return;
	}

	// 另一只手正拿着它：直接交接，不给速度
	if (AToyHand* Other = Grabbable->GetHolder())
	{
		Other->ReleaseHeld(false);
	}

	// 先用世界坐标算好目标，再挂上去；挂的时候保持世界位置不变，然后在 SnapTime 内滑到目标
	FToyGripContext Context;
	Context.GripWorld = GripPoint->GetComponentTransform();
	Context.GripWorld.SetScale3D(FVector::OneVector);
	Context.PinchInGrip = Context.GripWorld.InverseTransformPosition(PinchPoint->GetComponentLocation());
	const FTransform Hold = Grabbable->ComputeHoldTransform(Context);

	Grabbable->OnGrabbed(this);
	Target->AttachToComponent(GripPoint, FAttachmentTransformRules::KeepWorldTransform);

	SnapFrom = Target->GetRootComponent()->GetRelativeTransform();
	SnapTo = Hold;
	SnapTo.SetScale3D(SnapFrom.GetScale3D());
	SnapAlpha = SnapTime > 0.f ? 0.f : 1.f;
	if (SnapAlpha >= 1.f)
	{
		Target->GetRootComponent()->SetRelativeTransform(SnapTo);
	}

	HeldActor = Target;
	ShowInfo(Target);
	Pulse(0.7f, 0.08f);
}

void AToyHand::ReleaseHeld(bool bThrow)
{
	AActor* Target = HeldActor.Get();
	HeldActor.Reset();
	ShowInfo(nullptr);
	if (!Target)
	{
		return;
	}
	Target->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (IToyGrabbable* Grabbable = AsGrabbable(Target))
	{
		Grabbable->OnReleased(this, bThrow ? GetThrowVelocity() : FVector::ZeroVector);
	}
}

void AToyHand::UpdateHover()
{
	AActor* Candidate = IsHolding() ? nullptr : FindGrabCandidate();
	if (Candidate == HoveredActor.Get())
	{
		return;
	}
	if (IToyGrabbable* Previous = AsGrabbable(HoveredActor.Get()))
	{
		Previous->RemoveHover();
	}
	HoveredActor = Candidate;
	if (IToyGrabbable* Grabbable = AsGrabbable(Candidate))
	{
		Grabbable->AddHover();
	}
}

void AToyHand::ShowInfo(AActor* Target)
{
	FText Title;
	FText Body;
	const IToyGrabbable* Grabbable = AsGrabbable(Target);
	const bool bShow = Grabbable && InfoTitle.IsValid() && Grabbable->GetGrabInfo(Title, Body);
	if (bShow)
	{
		InfoTitle->SetText(Title);
		InfoBody->SetText(Body);
	}
	InfoLabel->SetHiddenInGame(!bShow);
}

void AToyHand::TickInfo()
{
	AActor* Target = HeldActor.Get();
	if (!Target || InfoLabel->bHiddenInGame)
	{
		return;
	}
	// 摆在拿着的东西正上方 6cm（牌子的底边对齐这里），整个牌子转过来正对玩家的眼睛
	FVector Origin;
	FVector Extent;
	Target->GetActorBounds(true, Origin, Extent);
	const FVector Anchor(Origin.X, Origin.Y, Origin.Z + Extent.Z + 6.f);

	FVector Eye = Anchor - FVector(100.f, 0.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Eye = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	InfoLabel->SetWorldLocationAndRotation(Anchor, (Eye - Anchor).Rotation());
}

void AToyHand::TickHeld(float DeltaSeconds)
{
	AActor* Target = HeldActor.Get();
	if (!Target || SnapAlpha >= 1.f)
	{
		return;
	}
	SnapAlpha = FMath::Min(1.f, SnapAlpha + DeltaSeconds / SnapTime);
	FTransform Blended;
	Blended.Blend(SnapFrom, SnapTo, FMath::InterpEaseOut(0.f, 1.f, SnapAlpha, 2.f));
	Target->GetRootComponent()->SetRelativeTransform(Blended);
}

void AToyHand::TickCurls(float DeltaSeconds)
{
	// 拿着东西时按抓法定手指的最小弯曲：全手握 = HoldCurl；捏 = 食指半弯、其余三指收起；托整机 = 手指微弯
	float MinFinger = 0.f;
	float MinIndex = 0.f;
	float ThumbCurl = -1.f;
	if (const IToyGrabbable* Held = AsGrabbable(HeldActor.Get()))
	{
		switch (Held->GetGripStyle())
		{
		case EToyGripStyle::Power: MinFinger = HoldCurl; MinIndex = HoldCurl; break;
		case EToyGripStyle::Pinch: MinFinger = MaxCurl; MinIndex = 35.f; ThumbCurl = 55.f; break;
		case EToyGripStyle::Palm:  MinFinger = 20.f; MinIndex = 20.f; break;
		}
	}
	FingerCurl = FMath::FInterpTo(FingerCurl, FMath::Max(Grip * MaxCurl, MinFinger), DeltaSeconds, 20.f);
	// 捏的时候握把按满食指也不弯到底，保持捏的样子
	const float IndexTarget = (ThumbCurl >= 0.f) ? MinIndex : FMath::Max(Trigger * MaxCurl, MinIndex);
	IndexCurl = FMath::FInterpTo(IndexCurl, IndexTarget, DeltaSeconds, 20.f);

	// 绕 Z 轴负方向转 = 手指朝掌心（-Y）弯；左手在镜像空间里自动变成反方向
	for (int32 Finger = 0; Finger < 4; ++Finger)
	{
		const float Curl = (Finger == 0) ? IndexCurl : FingerCurl;
		FingerJoints[Finger * 2]->SetRelativeRotation(FRotator(0.f, -Curl, 0.f));
		FingerJoints[Finger * 2 + 1]->SetRelativeRotation(FRotator(0.f, -Curl * 1.1f, 0.f));
	}
	// 拇指平时跟着另外三指；捏的时候往食指方向收一个固定角度
	const float Thumb = (ThumbCurl >= 0.f) ? ThumbCurl : FingerCurl;
	ThumbBase->SetRelativeRotation(ThumbOpen + FRotator(-Thumb * 0.2f, -Thumb * 0.4f, 0.f));
	ThumbMid->SetRelativeRotation(FRotator(0.f, -Thumb * 0.6f, 0.f));
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
	if (IToyGrabbable* Previous = AsGrabbable(HoveredActor.Get()))
	{
		Previous->RemoveHover();
	}
	Super::EndPlay(EndPlayReason);
}
