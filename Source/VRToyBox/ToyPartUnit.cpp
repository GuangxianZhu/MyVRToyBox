#include "ToyPartUnit.h"

#include "ToyExplodedModel.h"
#include "ToyHand.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	float EaseInOutCubic(float X)
	{
		return X < 0.5f ? 4.f * X * X * X : 1.f - FMath::Pow(-2.f * X + 2.f, 3.f) * 0.5f;
	}

	/** 单个步骤在总进度 T 下走完了多少（0~1）。每步在 [出场时刻×0.6, 出场时刻×0.6+0.4] 里缓动走完。 */
	float StepProgress(float T, float Start)
	{
		return EaseInOutCubic(FMath::Clamp((T - Start * 0.6f) / 0.4f, 0.f, 1.f));
	}

	/** 在若干候选方向里挑和 Want 最一致的那个。 */
	FVector PickClosest(const FVector& Want, std::initializer_list<FVector> Candidates)
	{
		FVector Best = *Candidates.begin();
		float BestDot = -2.f;
		for (const FVector& Candidate : Candidates)
		{
			const float Dot = FVector::DotProduct(Candidate, Want);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				Best = Candidate;
			}
		}
		return Best;
	}
}

AToyPartUnit::AToyPartUnit()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	// 只做查询：手的球形重叠查 WorldDynamic。鼠标射线（Visibility）不打它，打零件网格本身（见 AddMesh），
	// 否则零件挨得近时，盒子会挡住后面真正点到的那个零件
	GrabBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GrabBox"));
	GrabBox->SetupAttachment(Root);
	GrabBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrabBox->SetCollisionObjectType(ECC_WorldDynamic);
	GrabBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	GrabBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GrabBox->SetGenerateOverlapEvents(false);
}

void AToyPartUnit::Setup(AToyExplodedModel* InModel, const FString& InId, const FText& InTitle, const FText& InBody,
	EToyPartGrab InGrab, TArray<FToyCylinderSegment> InSegments, const FBox& InGrabBox)
{
	Model = InModel;
	Id = InId;
	Title = InTitle;
	Body = InBody;
	Grab = InGrab;
	Segments = MoveTemp(InSegments);
	BoxShape = InGrabBox;
}

UStaticMeshComponent* AToyPartUnit::AddMesh(UStaticMesh* Mesh, const FString& PartName)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, *PartName);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetStaticMesh(Mesh);
	// 网格只挡 Visibility 射线（桌面模式鼠标按三角形精确点中零件），不参与物理、不被手的重叠查到
	Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Part->SetCollisionObjectType(ECC_WorldStatic);
	Part->SetCollisionResponseToAllChannels(ECR_Ignore);
	Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Part->SetupAttachment(Root);
	Part->RegisterComponent();
	Meshes.Add(Part);
	LocalBounds += Mesh->GetBoundingBox();
	return Part;
}

void AToyPartUnit::SetExplodeSteps(const TArray<FToyExplodeStep>& InSteps)
{
	Steps = InSteps;
}

void AToyPartUnit::FinishSetup()
{
	if (!LocalBounds.IsValid)
	{
		return;
	}
	// 盒子在单元的本地坐标（模型坐标）里，世界大小 = 本地 × 模型缩放；太薄的零件给一个最小厚度
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(GetActorScale3D().GetMax()));
	const FVector MinExtent(MinGrabHalfExtent / Scale);
	GrabBox->SetRelativeLocation(LocalBounds.GetCenter());
	GrabBox->SetBoxExtent(LocalBounds.GetExtent().ComponentMax(MinExtent));
	if (!BoxShape.IsValid)
	{
		BoxShape = LocalBounds;
	}
}

FVector AToyPartUnit::GetExplodeOffset(float T) const
{
	FVector Offset = FVector::ZeroVector;
	for (const FToyExplodeStep& Step : Steps)
	{
		Offset += Step.Offset * StepProgress(T, Step.Start);
	}
	return Offset;
}

bool AToyPartUnit::IsHome() const
{
	return !Holder.IsValid() && !bReturning;
}

void AToyPartUnit::SetGrabEnabled(bool bEnabled)
{
	bGrabEnabled = bEnabled && IsGrabbableUnit();
	GrabBox->SetCollisionEnabled(bGrabEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

bool AToyPartUnit::CanGrab() const
{
	return bGrabEnabled;
}

void AToyPartUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AToyExplodedModel* ModelActor = Model.Get();
	if (!ModelActor)
	{
		return;
	}

	if (Holder.IsValid())
	{
		// 拿着靠近原位：原位出现虚影，提示"松手就装回去了"
		const bool bNear = DistanceToHome() < MagnetDistance;
		if (bNear != bGhostShown)
		{
			bGhostShown = bNear;
			ModelActor->ShowGhost(this, bNear);
		}
		return;
	}

	if (bReturning)
	{
		// 目标每帧重新取：飞回去的路上模型可能正在爆炸 / 收回，原位一直在变
		ReturnAlpha = FMath::Min(1.f, ReturnAlpha + DeltaSeconds / FMath::Max(0.01f, ReturnDuration));
		const FTransform Target = ModelActor->GetUnitHomeWorld(this);
		const float Alpha = FMath::InterpEaseInOut(0.f, 1.f, ReturnAlpha, 2.f);
		FTransform Blended;
		Blended.SetLocation(FMath::Lerp(ReturnFrom.GetLocation(), Target.GetLocation(), Alpha));
		Blended.SetRotation(FQuat::Slerp(ReturnFrom.GetRotation(), Target.GetRotation(), Alpha));
		Blended.SetScale3D(Target.GetScale3D());
		SetActorTransform(Blended);
		if (ReturnAlpha >= 1.f)
		{
			bReturning = false;
			ModelActor->OnUnitHome(this);
		}
	}
}

float AToyPartUnit::DistanceToHome() const
{
	const AToyExplodedModel* ModelActor = Model.Get();
	if (!ModelActor)
	{
		return TNumericLimits<float>::Max();
	}
	const FVector Now = GetActorTransform().TransformPosition(GetLocalCenter());
	const FVector Home = ModelActor->GetUnitHomeWorld(this).TransformPosition(GetLocalCenter());
	return FVector::Dist(Now, Home);
}

FTransform AToyPartUnit::FrameToHold(const FTransform& FrameWorld) const
{
	// 和方块一样：抓点世界变换 = 手抓点世界变换 → 物体相对手抓点 = (抓点相对物体)⁻¹。
	// 物体变换先去掉缩放再算，缩放由手在挂上去时补回
	const FTransform ObjectWorld(GetActorQuat(), GetActorLocation());
	return FrameWorld.GetRelativeTransform(ObjectWorld).Inverse();
}

FTransform AToyPartUnit::CylinderFrame(const FToyGripContext& Grip) const
{
	const FTransform ObjectWorld = GetActorTransform();
	const FVector GripLocation = Grip.GripWorld.GetLocation();
	const FVector GripLocal = ObjectWorld.InverseTransformPosition(GripLocation);

	// 1) 绕轴的哪一侧贴手：优先让圆柱面外法线 = 手背方向（手掌正对零件，绕轴转零件看不出来，所以这一步"免费"）；
	//    手背几乎顺着轴（手掌对着轴端）时，退回到"手在零件的哪一侧"
	const FVector HandXLocal = ObjectWorld.InverseTransformVectorNoScale(Grip.GripWorld.GetUnitAxis(EAxis::X));
	FVector Radial(0.f, HandXLocal.Y, HandXLocal.Z);
	if (Radial.Size() < 0.3f)
	{
		Radial = FVector(0.f, GripLocal.Y, GripLocal.Z);
	}
	Radial = Radial.SizeSquared() > 1e-4f ? Radial.GetSafeNormal() : FVector::UpVector;

	//    沿轴取手的投影（两头留 3cm 给手掌），在几段圆柱里挑离手最近的
	FVector BestSurface = FVector::ZeroVector;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FToyCylinderSegment& Segment : Segments)
	{
		const float Margin = FMath::Min(3.f, (Segment.X1 - Segment.X0) * 0.5f);
		const float X = FMath::Clamp(static_cast<float>(GripLocal.X), Segment.X0 + Margin, Segment.X1 - Margin);
		const FVector Surface = ObjectWorld.TransformPosition(FVector(X, 0.f, 0.f) + Radial * Segment.Radius);
		const float Distance = FVector::Dist(Surface, GripLocation);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestSurface = Surface;
		}
	}

	// 2) 手掌贴圆柱面：手的 X（掌心→手背）= 圆柱面外法线。
	//    手指方向在 ±绕轴切线（握住一根轴）和 ±沿轴（扶着端盖外缘）四种里挑和现在手指最一致的，抓起时转得最少
	const FVector Normal = ObjectWorld.TransformVectorNoScale(Radial).GetSafeNormal();
	const FVector Axis = ObjectWorld.TransformVectorNoScale(FVector::XAxisVector).GetSafeNormal();
	const FVector Tangent = FVector::CrossProduct(Axis, Normal).GetSafeNormal();
	const FVector HandZ = Grip.GripWorld.GetUnitAxis(EAxis::Z);
	const FVector FingerDir = PickClosest(HandZ, { Tangent, -Tangent, Axis, -Axis });
	return FTransform(FRotationMatrix::MakeFromXZ(Normal, FingerDir).ToQuat(), BestSurface);
}

FTransform AToyPartUnit::BoxFrame(const FToyGripContext& Grip) const
{
	// 和方块的 6 个抓点同一个规则：面朝向越接近手掌越好、离手越近越好；再在面内 4 种摆法里挑最贴合手指方向的
	const FTransform ObjectWorld = GetActorTransform();
	const FVector HandX = Grip.GripWorld.GetUnitAxis(EAxis::X);
	const FVector HandZ = Grip.GripWorld.GetUnitAxis(EAxis::Z);
	const FVector GripLocation = Grip.GripWorld.GetLocation();
	const FVector Center = BoxShape.GetCenter();
	const FVector Extent = BoxShape.GetExtent();
	const float Size = FMath::Max(1.f, static_cast<float>(ObjectWorld.TransformVector(Extent).GetAbsMax()) * 2.f);

	FTransform Best;
	float BestScore = TNumericLimits<float>::Lowest();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (const float Sign : { 1.f, -1.f })
		{
			FVector LocalNormal = FVector::ZeroVector;
			LocalNormal[Axis] = Sign;
			const FVector FaceCenter = ObjectWorld.TransformPosition(Center + LocalNormal * Extent[Axis]);
			const FVector Normal = ObjectWorld.TransformVectorNoScale(LocalNormal).GetSafeNormal();
			const float Score = FVector::DotProduct(Normal, HandX) - 0.5f * FVector::Dist(FaceCenter, GripLocation) / Size;
			if (Score <= BestScore)
			{
				continue;
			}
			BestScore = Score;
			FVector LocalU = FVector::ZeroVector;
			LocalU[(Axis + 1) % 3] = 1.f;
			const FVector U = ObjectWorld.TransformVectorNoScale(LocalU).GetSafeNormal();
			const FVector V = FVector::CrossProduct(Normal, U).GetSafeNormal();
			const FVector FingerDir = PickClosest(HandZ, { U, -U, V, -V });
			Best = FTransform(FRotationMatrix::MakeFromXZ(Normal, FingerDir).ToQuat(), FaceCenter);
		}
	}
	return Best;
}

FTransform AToyPartUnit::ComputeHoldTransform(const FToyGripContext& Grip) const
{
	switch (Grab)
	{
	case EToyPartGrab::Cylinder:
		return FrameToHold(CylinderFrame(Grip));
	case EToyPartGrab::Box:
		return FrameToHold(BoxFrame(Grip));
	case EToyPartGrab::Pinch:
	default:
	{
		// 捏：朝向保持抓起瞬间的样子（小件怎么转都行），只把零件中心挪到拇指和食指之间
		const FTransform ObjectWorld(GetActorQuat(), GetActorLocation());
		FTransform Hold = ObjectWorld.GetRelativeTransform(Grip.GripWorld);
		const FVector CenterWorld = GetActorTransform().TransformPosition(GetLocalCenter());
		const FVector CenterInGrip = Grip.GripWorld.InverseTransformPosition(CenterWorld);
		Hold.AddToTranslation(Grip.PinchInGrip - CenterInGrip);
		return Hold;
	}
	}
}

void AToyPartUnit::OnGrabbed(AToyHand* Hand)
{
	Holder = Hand;
	bReturning = false;
	bGhostShown = false;
}

void AToyPartUnit::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	Holder.Reset();
	// 零件不扔：不管速度多大，都飞回原位；离原位很近时快速吸进去
	const float Distance = DistanceToHome();
	ReturnDuration = Distance < MagnetDistance ? SnapReturnTime : ReturnTime;
	if (const AToyExplodedModel* ModelActor = Model.Get())
	{
		const FVector Offset = ModelActor->GetUnitHomeWorld(this).TransformPosition(GetLocalCenter()) - GetActorTransform().TransformPosition(GetLocalCenter());
		UE_LOG(LogTemp, Log, TEXT("ToyBox: %s 松手，离原位 %.1f cm（到原位 %s）%s"), *Id, Distance, *Offset.ToCompactString(),
			Distance < MagnetDistance ? TEXT("，吸回") : TEXT(""));
	}
	ReturnFrom = GetActorTransform();
	ReturnAlpha = 0.f;
	bReturning = true;
	if (bGhostShown)
	{
		bGhostShown = false;
		if (AToyExplodedModel* ModelActor = Model.Get())
		{
			ModelActor->ShowGhost(this, false);
		}
	}
}

AToyHand* AToyPartUnit::GetHolder() const
{
	return Holder.Get();
}

void AToyPartUnit::AddHover()
{
	++HoverCount;
	RefreshOverlay();
}

void AToyPartUnit::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	RefreshOverlay();
}

void AToyPartUnit::SetOverlay(UMaterialInterface* Overlay)
{
	ModelOverlay = Overlay;
	RefreshOverlay();
}

void AToyPartUnit::RefreshOverlay()
{
	UMaterialInterface* Overlay = ModelOverlay.Get();
	if (HoverCount > 0)
	{
		if (const AToyExplodedModel* ModelActor = Model.Get())
		{
			Overlay = ModelActor->GetHoverMaterial();
		}
	}
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Mesh->SetOverlayMaterial(Overlay);
	}
}

EToyGripStyle AToyPartUnit::GetGripStyle() const
{
	return Grab == EToyPartGrab::Pinch ? EToyGripStyle::Pinch : EToyGripStyle::Power;
}

bool AToyPartUnit::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = Title;
	OutBody = Body;
	return !Title.IsEmpty();
}
