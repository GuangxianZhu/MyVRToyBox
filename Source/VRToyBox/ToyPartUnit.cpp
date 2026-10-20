#include "ToyPartUnit.h"

#include "ToyExplodedModel.h"
#include "ToyHand.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "DrawDebugHelpers.h"
#include "ToyCube.h"

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

void AToyPartUnit::Setup(AToyExplodedModel* InModel, const FString& InId, const FText& InTitle, const FText& InBody, const FToyGrabSpec& InGrab)
{
	Model = InModel;
	Id = InId;
	Title = InTitle;
	Body = InBody;
	Grab = InGrab;
	Grab.Axis = Grab.Axis.GetSafeNormal();
	if (Grab.Axis.IsNearlyZero())
	{
		Grab.Axis = FVector::XAxisVector;
	}
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
	if (!Grab.Box.IsValid)
	{
		Grab.Box = LocalBounds;
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
	// 以圆心为原点、沿轴量的本地坐标（模型坐标，未缩放）
	const FVector AxisLocal = Grab.Axis;
	const FVector GripLocal = ObjectWorld.InverseTransformPosition(GripLocation) - Grab.Center;
	const float GripAlong = FVector::DotProduct(GripLocal, AxisLocal);

	// 1) 绕轴的哪一侧贴手：优先让圆柱面外法线 = 手背方向（手掌正对零件，绕轴转零件看不出来，所以这一步"免费"）；
	//    手背几乎顺着轴（手掌对着轴端）时，退回到"手在零件的哪一侧"
	const FVector HandXLocal = ObjectWorld.InverseTransformVectorNoScale(Grip.GripWorld.GetUnitAxis(EAxis::X));
	FVector Radial = HandXLocal - AxisLocal * FVector::DotProduct(HandXLocal, AxisLocal);
	if (Radial.Size() < 0.3f)
	{
		Radial = GripLocal - AxisLocal * GripAlong;
	}
	if (Radial.SizeSquared() < 1e-4f)
	{
		// 手正好在轴上：随便取一个垂直于轴的方向
		Radial = FVector::CrossProduct(AxisLocal, FMath::Abs(AxisLocal.Z) < 0.9f ? FVector::UpVector : FVector::RightVector);
	}
	Radial = Radial.GetSafeNormal();

	//    沿轴取手的投影（两头留 3cm 给手掌），在几段圆柱里挑离手最近的
	FVector BestSurface = FVector::ZeroVector;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FToyCylinderSegment& Segment : Grab.Segments)
	{
		const float Margin = FMath::Min(3.f, (Segment.X1 - Segment.X0) * 0.5f);
		const float X = FMath::Clamp(GripAlong, Segment.X0 + Margin, Segment.X1 - Margin);
		const FVector Surface = ObjectWorld.TransformPosition(Grab.Center + AxisLocal * X + Radial * Segment.Radius);
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
	const FVector Axis = ObjectWorld.TransformVectorNoScale(AxisLocal).GetSafeNormal();
	const FVector Tangent = FVector::CrossProduct(Axis, Normal).GetSafeNormal();
	const FVector HandZ = Grip.GripWorld.GetUnitAxis(EAxis::Z);
	//    细长的杆（阀杆、齿轮轴、转子：长度超过半径的 2.5 倍）只能"握住"——手指绕着杆弯，不能顺着杆伸，
	//    不然杆藏在手掌底下、看起来像托着而不是握着
	float Length = 0.f;
	float Radius = 0.f;
	for (const FToyCylinderSegment& Segment : Grab.Segments)
	{
		Length += Segment.X1 - Segment.X0;
		Radius = FMath::Max(Radius, Segment.Radius);
	}
	const FVector FingerDir = Length > 2.5f * Radius
		? PickClosest(HandZ, { Tangent, -Tangent })
		: PickClosest(HandZ, { Tangent, -Tangent, Axis, -Axis });
	return FTransform(FRotationMatrix::MakeFromXZ(Normal, FingerDir).ToQuat(), BestSurface);
}

FTransform AToyPartUnit::BoxFrame(const FToyGripContext& Grip) const
{
	// 和方块的 6 个抓点同一个规则：面朝向越接近手掌越好、离手越近越好；再在面内 4 种摆法里挑最贴合手指方向的
	const FTransform ObjectWorld = GetActorTransform();
	const FVector HandX = Grip.GripWorld.GetUnitAxis(EAxis::X);
	const FVector HandZ = Grip.GripWorld.GetUnitAxis(EAxis::Z);
	const FVector GripLocation = Grip.GripWorld.GetLocation();
	const FVector Center = Grab.Box.GetCenter();
	const FVector Extent = Grab.Box.GetExtent();
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

bool AToyPartUnit::TraceUnit(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	// 只打这个单元自己的网格，按三角形（复杂碰撞）打，不管周围别的零件
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyPartSurface), true);
	bool bAny = false;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		FHitResult Hit;
		if (Mesh && Mesh->LineTraceComponent(Hit, Start, End, Params) && (!bAny || Hit.Distance < OutHit.Distance))
		{
			OutHit = Hit;
			bAny = true;
		}
	}
	return bAny;
}

bool AToyPartUnit::SurfaceFrame(const FToyGripContext& Grip, FTransform& OutFrame) const
{
	const FVector HandX = Grip.GripWorld.GetUnitAxis(EAxis::X);
	const FVector HandZ = Grip.GripWorld.GetUnitAxis(EAxis::Z);
	const FVector Pinch = Grip.GripWorld.TransformPosition(Grip.PinchInGrip);
	const float Reach = FMath::Max(10.f, static_cast<float>(GetActorTransform().TransformVector(LocalBounds.GetExtent()).GetAbsMax()) * 2.f);

	// 1) 从指尖稍微靠手背的位置，沿"掌心朝外"（-手背方向）往零件里打：手掌正对着的那块真实表面
	FHitResult Hit;
	bool bHit = TraceUnit(Pinch + HandX * 5.f, Pinch - HandX * Reach, Hit);
	// 2) 掌心没对着零件（比如手侧着伸过去）：从指尖朝零件中心打
	if (!bHit)
	{
		const FVector CenterWorld = GetActorTransform().TransformPosition(GetLocalCenter());
		const FVector Dir = (CenterWorld - Pinch).GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			bHit = TraceUnit(Pinch - Dir * 5.f, CenterWorld + Dir * Reach, Hit);
		}
	}
	// 3) 指尖正好在零件的孔里 / 两片之间（阀门端盖中间的孔）：往 26 个方向找最近的真实表面
	if (!bHit)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					const FVector Dir = FVector(X, Y, Z).GetSafeNormal();
					FHitResult Probe;
					if (!Dir.IsNearlyZero() && TraceUnit(Pinch, Pinch + Dir * Reach, Probe) && (!bHit || Probe.Distance < Hit.Distance))
					{
						Hit = Probe;
						bHit = true;
					}
				}
			}
		}
	}
	if (!bHit)
	{
		return false;
	}

	// 手掌贴在命中点：手的 X（掌心→手背）= 表面外法线（朝着手这一侧），手指方向取现在的手指投影到表面上
	// 法线朝着射线来的方向（手这一侧）。不能用"指向捏合点"判断：捏合点常常已经陷进零件表面一点，会把顶面法线翻到下面去
	FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	if (FVector::DotProduct(Normal, Hit.TraceEnd - Hit.TraceStart) > 0.f)
	{
		Normal = -Normal; // 打到背面（单面网格）时翻过来
	}
	FVector FingerDir = HandZ - Normal * FVector::DotProduct(HandZ, Normal);
	if (FingerDir.SizeSquared() < 1e-3f)
	{
		FingerDir = FVector::CrossProduct(Normal, Grip.GripWorld.GetUnitAxis(EAxis::Y));
	}
	OutFrame = FTransform(FRotationMatrix::MakeFromXZ(Normal, FingerDir.GetSafeNormal()).ToQuat(), Hit.ImpactPoint);
	if (ToyBox::ShowGrabPoints())
	{
		DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint, Hit.ImpactPoint + Normal * 6.f, 2.f, FColor::Cyan, false, 3.f);
	}
	return true;
}

FTransform AToyPartUnit::PinchHold(const FToyGripContext& Grip) const
{
	// 捏：朝向保持抓起瞬间的样子（小件怎么转都行），只把零件中心（或覆盖文件指定的捏点）挪到拇指和食指之间
	const FTransform ObjectWorld(GetActorQuat(), GetActorLocation());
	FTransform Hold = ObjectWorld.GetRelativeTransform(Grip.GripWorld);
	const FVector PointLocal = Grab.Point.Get(GetLocalCenter());
	const FVector PointWorld = GetActorTransform().TransformPosition(PointLocal);
	const FVector PointInGrip = Grip.GripWorld.InverseTransformPosition(PointWorld);
	Hold.AddToTranslation(Grip.PinchInGrip - PointInGrip);
	return Hold;
}

FTransform AToyPartUnit::ComputeHoldTransform(const FToyGripContext& Grip) const
{
	// 自由捏取（Meta Interaction SDK 捏取的做法）：小件把中心吸到捏合点；其余零件保持抓起那一刻相对手的位置和角度，
	// 不再把手掌吸到表面上。parts.json 里的 cylinder / box / surface 数据留着，toy.PalmSnap 1 时还用旧的贴合
	if (Grab.Type == EToyPartGrab::Pinch)
	{
		return PinchHold(Grip);
	}
	if (!ToyBox::PalmSnap())
	{
		return FTransform(GetActorQuat(), GetActorLocation()).GetRelativeTransform(Grip.GripWorld);
	}
	switch (Grab.Type)
	{
	case EToyPartGrab::Cylinder:
		return FrameToHold(CylinderFrame(Grip));
	case EToyPartGrab::Box:
		return FrameToHold(BoxFrame(Grip));
	case EToyPartGrab::Surface:
	{
		FTransform Frame;
		if (SurfaceFrame(Grip, Frame))
		{
			return FrameToHold(Frame);
		}
		// 哪个方向都没碰到（几乎不会）：抓哪里就保持哪里
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: %s 贴表面没打到网格，改成原样抓"), *Id);
		return FTransform(GetActorQuat(), GetActorLocation()).GetRelativeTransform(Grip.GripWorld);
	}
	case EToyPartGrab::Pinch:
	default:
		return PinchHold(Grip);
	}
}

FString AToyPartUnit::DescribeGrab() const
{
	// 调试截图上的说明文字：调试字体没有中文，用英文
	FString Text;
	switch (Grab.Type)
	{
	case EToyPartGrab::Cylinder: Text = FString::Printf(TEXT("cylinder axis(%.0f,%.0f,%.0f)"), Grab.Axis.X, Grab.Axis.Y, Grab.Axis.Z); break;
	case EToyPartGrab::Box:      Text = TEXT("box"); break;
	case EToyPartGrab::Pinch:    Text = Grab.Point.IsSet() ? TEXT("pinch@point") : TEXT("pinch"); break;
	case EToyPartGrab::Surface:  Text = TEXT("surface"); break;
	default:                     Text = TEXT("none"); break;
	}
	if (!Grab.Override.IsEmpty())
	{
		Text += TEXT(" [override]");
	}
	return Text;
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
	return EToyGripStyle::Pinch;
}

bool AToyPartUnit::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = Title;
	OutBody = Body;
	return !Title.IsEmpty();
}
