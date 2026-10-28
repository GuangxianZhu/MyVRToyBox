#include "ToyExplodedModel.h"

#include "ToyHand.h"
#include "ToyLightGroupComponent.h"
#include "ToyPartUnit.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Tools/create_toy_materials.py 建的半透明材质：参数 Color、Opacity。 */
	const TCHAR* GhostMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float EmissiveBoost = 12.f;

	EToyPartGrab ParseGrabType(const FString& Type)
	{
		if (Type == TEXT("cylinder")) return EToyPartGrab::Cylinder;
		if (Type == TEXT("box"))      return EToyPartGrab::Box;
		if (Type == TEXT("pinch"))    return EToyPartGrab::Pinch;
		if (Type == TEXT("surface"))  return EToyPartGrab::Surface;
		return EToyPartGrab::None;
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values) && Values->Num() >= 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}
}

AToyExplodedModel::AToyExplodedModel()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	// 所有零件挂在一个光照组根下面：整个模型只渲一张固定太阳光的逐物体阴影，不是每个零件一张（见 UToyLightGroupComponent）
	ModelRoot = CreateDefaultSubobject<UToyLightGroupComponent>(TEXT("ModelRoot"));
	ModelRoot->SetupAttachment(Root);

	// 整机的检测盒：和零件的一样只做查询（手的重叠查 WorldDynamic，桌面鼠标射线查 Visibility）
	GrabBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GrabBox"));
	GrabBox->SetupAttachment(ModelRoot);
	GrabBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrabBox->SetCollisionObjectType(ECC_WorldDynamic);
	GrabBox->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 鼠标射线打零件网格本身，不打这个大盒子
	GrabBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GrabBox->SetGenerateOverlapEvents(false);
}

bool AToyExplodedModel::LoadManifest(const FString& ManifestPath)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 找不到模型清单 %s，先运行 导入电机模型.bat"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	const TArray<TSharedPtr<FJsonValue>>* PartValues = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("parts"), PartValues))
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 模型清单格式不对 %s"), *ManifestPath);
		return false;
	}

	// explode_scale：爆炸距离整体缩放（阀门的网页版爆炸距离按全屏定的，放大摆到桌上会飞出视野）
	double ExplodeScale = 1.0;
	const TSharedPtr<FJsonObject>* ModelInfo = nullptr;
	if (Manifest->TryGetObjectField(TEXT("model"), ModelInfo))
	{
		ModelTitle = FText::FromString((*ModelInfo)->GetStringField(TEXT("title")));
		ModelBody = FText::FromString((*ModelInfo)->GetStringField(TEXT("body")));
		(*ModelInfo)->TryGetNumberField(TEXT("explode_scale"), ExplodeScale);
		// length_cm：这个模型自己要的展示尺寸（万用表 40cm），没写就用工作室给的 TargetLengthCm
		double LengthCm = 0.0;
		if ((*ModelInfo)->TryGetNumberField(TEXT("length_cm"), LengthCm) && LengthCm > 1.0)
		{
			TargetLengthCm = static_cast<float>(LengthCm);
		}
	}

	// 1) 抓取单元。单元 Actor 以单位变换挂在 ModelRoot 上，继承它的缩放
	TMap<FString, AToyPartUnit*> UnitById;
	auto SpawnUnit = [this, &UnitById](const FString& Id, const TSharedPtr<FJsonObject>& Info) -> AToyPartUnit*
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AToyPartUnit* Unit = GetWorld()->SpawnActor<AToyPartUnit>(AToyPartUnit::StaticClass(), ModelRoot->GetComponentTransform(), Params);
		if (!Unit)
		{
			return nullptr;
		}
		Unit->AttachToComponent(ModelRoot, FAttachmentTransformRules::SnapToTargetIncludingScale);

		FToyGrabSpec Grab;
		FText Title;
		FText Body;
		const TSharedPtr<FJsonObject>* GrabObject = nullptr;
		if (Info.IsValid())
		{
			Title = FText::FromString(Info->GetStringField(TEXT("name_cn")));
			const FString Material = Info->GetStringField(TEXT("material"));
			const FString Desc = Info->GetStringField(TEXT("desc"));
			Body = FText::FromString(Material.IsEmpty() ? Desc : FString::Printf(TEXT("材料：%s\n%s"), *Material, *Desc));
			if (Info->GetBoolField(TEXT("grabbable")) && Info->TryGetObjectField(TEXT("grab"), GrabObject))
			{
				const TSharedPtr<FJsonObject>& G = *GrabObject;
				Grab.Type = ParseGrabType(G->GetStringField(TEXT("type")));
				Grab.Override = G->HasField(TEXT("override")) ? G->GetStringField(TEXT("override")) : FString();
				const TArray<TSharedPtr<FJsonValue>>* SegmentValues = nullptr;
				if (G->TryGetArrayField(TEXT("segments"), SegmentValues))
				{
					for (const TSharedPtr<FJsonValue>& SegmentValue : *SegmentValues)
					{
						const TArray<TSharedPtr<FJsonValue>>& N = SegmentValue->AsArray();
						if (N.Num() >= 3)
						{
							Grab.Segments.Add(FToyCylinderSegment{ static_cast<float>(N[0]->AsNumber()),
								static_cast<float>(N[1]->AsNumber()), static_cast<float>(N[2]->AsNumber()) });
						}
					}
				}
				// 圆柱：axis + center（老数据没有 → 模型 X 轴过原点）；方块：center + extent；捏：point
				if (G->HasField(TEXT("axis")))
				{
					Grab.Axis = ReadVector(G, TEXT("axis"));
				}
				if (G->HasField(TEXT("center")))
				{
					Grab.Center = ReadVector(G, TEXT("center"));
				}
				if (Grab.Type == EToyPartGrab::Box && G->HasField(TEXT("extent")))
				{
					Grab.Box = FBox::BuildAABB(Grab.Center, ReadVector(G, TEXT("extent")));
				}
				if (G->HasField(TEXT("point")))
				{
					Grab.Point = ReadVector(G, TEXT("point"));
				}
			}
		}
		Unit->Setup(this, Id, Title, Body, Grab);
		Units.Add(Unit);
		UnitById.Add(Id, Unit);
		return Unit;
	};

	const TArray<TSharedPtr<FJsonValue>>* UnitValues = nullptr;
	if (Manifest->TryGetArrayField(TEXT("units"), UnitValues))
	{
		for (const TSharedPtr<FJsonValue>& UnitValue : *UnitValues)
		{
			const TSharedPtr<FJsonObject> UnitObject = UnitValue->AsObject();
			if (UnitObject.IsValid())
			{
				SpawnUnit(UnitObject->GetStringField(TEXT("id")), UnitObject);
			}
		}
	}

	// 2) 零件网格挂到各自的单元上；旧清单没有 unit 字段时都算基座（只能看、不能抓）
	FBox Bounds(ForceInit);
	TSet<AToyPartUnit*> UnitsWithSteps;
	int32 PartCount = 0;
	for (const TSharedPtr<FJsonValue>& PartValue : *PartValues)
	{
		const TSharedPtr<FJsonObject> PartObject = PartValue->AsObject();
		if (!PartObject.IsValid())
		{
			continue;
		}
		const FString MeshPath = PartObject->GetStringField(TEXT("mesh"));
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: 加载不到网格 %s"), *MeshPath);
			continue;
		}

		FString UnitId;
		if (!PartObject->TryGetStringField(TEXT("unit"), UnitId))
		{
			UnitId = TEXT("base");
		}
		AToyPartUnit* Unit = UnitById.FindRef(UnitId);
		if (!Unit)
		{
			Unit = SpawnUnit(UnitId, nullptr);
		}
		if (!Unit)
		{
			continue;
		}
		Unit->AddMesh(Mesh, PartObject->GetStringField(TEXT("name")));
		Bounds += Mesh->GetBoundingBox();
		++PartCount;

		// 同一单元的零件爆炸路线相同（导入脚本就是按路线分的组），取第一个零件的
		if (!UnitsWithSteps.Contains(Unit))
		{
			UnitsWithSteps.Add(Unit);
			TArray<FToyExplodeStep> Steps;
			const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
			if (PartObject->TryGetArrayField(TEXT("ex"), StepValues))
			{
				for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
				{
					const TArray<TSharedPtr<FJsonValue>>& N = StepValue->AsArray();
					if (N.Num() >= 4)
					{
						Steps.Add(FToyExplodeStep{ FVector(N[0]->AsNumber(), N[1]->AsNumber(), N[2]->AsNumber()) * ExplodeScale,
							static_cast<float>(N[3]->AsNumber()) });
					}
				}
			}
			Unit->SetExplodeSteps(Steps);
		}
	}

	// 没分到网格的单元（清单里写了但网格都加载失败）删掉
	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		if (Units[Index]->GetMeshes().Num() == 0)
		{
			Units[Index]->Destroy();
			Units.RemoveAt(Index);
		}
	}

	if (PartCount == 0 || !Bounds.IsValid)
	{
		return false;
	}

	// 3) 按装配状态的最长边缩放；再把模型底面中心移到 Actor 原点，这样 Actor 放在桌面上模型就立在桌面上
	const float Scale = TargetLengthCm / FMath::Max(1.f, static_cast<float>(Bounds.GetSize().GetMax()));
	const FVector Center = Bounds.GetCenter();
	ModelRoot->SetRelativeScale3D(FVector(Scale));
	ModelRoot->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Bounds.Min.Z) * Scale);
	GrabBox->SetRelativeLocation(Center);
	GrabBox->SetBoxExtent(Bounds.GetExtent());
	for (AToyPartUnit* Unit : Units)
	{
		Unit->FinishSetup();
	}
	Ghosts.SetNum(Units.Num());

	// 4) 虚影 / 高亮材质。材质还没建（没跑 create_toy_materials.py）时就没有虚影和高亮，其他功能照常
	if (UMaterialInterface* Ghost = LoadObject<UMaterialInterface>(nullptr, GhostMaterialPath))
	{
		GhostMaterial = UMaterialInstanceDynamic::Create(Ghost, this);
		// 不受光照的材质亮度按"尼特"算：自动曝光是照室内灯光（几十尼特）调的，颜色值 1 看上去几乎是黑的，所以乘一个亮度
		GhostMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.85f, 1.f) * EmissiveBoost);
		GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
		HoverMaterial = UMaterialInstanceDynamic::Create(Ghost, this);
		HoverMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f) * EmissiveBoost);
		HoverMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 没有 %s，零件不显示高亮和虚影"), GhostMaterialPath);
	}

	int32 Grabbable = 0;
	for (const AToyPartUnit* Unit : Units)
	{
		Grabbable += Unit->IsGrabbableUnit() ? 1 : 0;
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 模型 %d 个零件、%d 个抓取单元（%d 个能抓），原始尺寸 %s cm，缩放 %.2f 倍"),
		PartCount, Units.Num(), Grabbable, *Bounds.GetSize().ToString(), Scale);
	ApplyProgress();
	UpdateGrabModes();
	return true;
}

void AToyExplodedModel::SetExploded(bool bInExploded)
{
	bExploded = bInExploded;
}

void AToyExplodedModel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickReturn(DeltaSeconds);

	const float Target = bExploded ? 1.f : 0.f;
	if (Progress != Target)
	{
		// 半路反向时从当前位置直接往回走，不会跳
		Progress = FMath::FInterpConstantTo(Progress, Target, DeltaSeconds, 1.f / FMath::Max(0.1f, ExplodeDuration));
		ApplyProgress();
	}
	UpdateGrabModes();
}

void AToyExplodedModel::ApplyProgress()
{
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AToyPartUnit* Unit = Units[Index];
		const FVector Offset = Unit->GetExplodeOffset(Progress);
		// 被抓着、或者正在飞回来的单元位置不归这里管
		if (Unit->IsHome())
		{
			Unit->SetActorRelativeLocation(Offset);
		}
		for (UStaticMeshComponent* Ghost : Ghosts[Index])
		{
			Ghost->SetRelativeLocation(Offset);
		}
	}
}

void AToyExplodedModel::UpdateGrabModes()
{
	// 完全装配：整机可抓；完全爆炸：零件可抓；动画中：都不能抓
	const int32 Mode = (!bExploded && Progress <= 0.f) ? 0 : ((bExploded && Progress >= 1.f) ? 1 : 2);
	if (Mode == GrabMode)
	{
		return;
	}
	GrabMode = Mode;
	GrabBox->SetCollisionEnabled(Mode == 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	for (AToyPartUnit* Unit : Units)
	{
		Unit->SetGrabEnabled(Mode == 1);
	}
}

FTransform AToyExplodedModel::GetUnitHomeWorld(const AToyPartUnit* Unit) const
{
	return FTransform(Unit->GetExplodeOffset(Progress)) * ModelRoot->GetComponentTransform();
}

void AToyExplodedModel::OnUnitHome(AToyPartUnit* Unit)
{
	Unit->AttachToComponent(ModelRoot, FAttachmentTransformRules::KeepWorldTransform);
	UToyLightGroupComponent::RefreshLightingGroup(Unit);
	// 飞回来的过程有插值误差，挂回去后直接写准确的相对变换，免得一点点漂
	Unit->SetActorRelativeTransform(FTransform(FQuat::Identity, Unit->GetExplodeOffset(Progress), FVector::OneVector));
}

void AToyExplodedModel::ShowGhost(AToyPartUnit* Unit, bool bShow)
{
	const int32 Index = Units.IndexOfByKey(Unit);
	if (Index == INDEX_NONE || !GhostMaterial)
	{
		return;
	}
	TArray<TObjectPtr<UStaticMeshComponent>>& UnitGhosts = Ghosts[Index];
	if (bShow && UnitGhosts.Num() == 0)
	{
		// 第一次用到才建：把单元的每个网格复制一份，所有材质槽都换成半透明
		for (const UStaticMeshComponent* Source : Unit->GetMeshes())
		{
			UStaticMeshComponent* Ghost = NewObject<UStaticMeshComponent>(this);
			Ghost->SetMobility(EComponentMobility::Movable);
			Ghost->SetStaticMesh(Source->GetStaticMesh());
			for (int32 Slot = 0; Slot < Ghost->GetNumMaterials(); ++Slot)
			{
				Ghost->SetMaterial(Slot, GhostMaterial);
			}
			Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ghost->SetCastShadow(false);
			Ghost->SetupAttachment(ModelRoot);
			Ghost->SetRelativeLocation(Unit->GetExplodeOffset(Progress));
			Ghost->RegisterComponent();
			UnitGhosts.Add(Ghost);
		}
	}
	for (UStaticMeshComponent* Ghost : UnitGhosts)
	{
		Ghost->SetVisibility(bShow);
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s 原位虚影%s"), *Unit->GetUnitId(), bShow ? TEXT("显示") : TEXT("隐藏"));
}

UMaterialInterface* AToyExplodedModel::GetHoverMaterial() const
{
	return HoverMaterial;
}

void AToyExplodedModel::ResetAll()
{
	// 先让所有手松开，零件和整机就会自己飞回原位
	if (AToyHand* Hand = Holder.Get())
	{
		Hand->ReleaseHeld(false);
	}
	for (AToyPartUnit* Unit : Units)
	{
		if (AToyHand* Hand = Unit->GetHolder())
		{
			Hand->ReleaseHeld(false);
		}
	}
}

bool AToyExplodedModel::CanGrab() const
{
	return GrabMode == 0;
}

FTransform AToyExplodedModel::ComputeHoldTransform(const FToyGripContext& Grip) const
{
	// 整机比手大得多，没有合适的"标准握法"：抓哪里就保持哪里（自由抓取），不吸附、不转
	return FTransform(GetActorQuat(), GetActorLocation()).GetRelativeTransform(Grip.GripWorld);
}

void AToyExplodedModel::OnGrabbed(AToyHand* Hand)
{
	if (!bReturning)
	{
		HomeParent = GetRootComponent()->GetAttachParent();
	}
	Holder = Hand;
	bReturning = false;
}

void AToyExplodedModel::OnReleased(AToyHand* Hand, const FVector& Velocity)
{
	Holder.Reset();
	ReturnFrom = GetActorTransform();
	ReturnAlpha = 0.f;
	bReturning = HomeParent.IsValid();
}

void AToyExplodedModel::TickReturn(float DeltaSeconds)
{
	USceneComponent* Home = HomeParent.Get();
	if (!bReturning || !Home)
	{
		return;
	}
	// 离支架 5cm 以内松手：快速吸回去；否则 0.4 秒飞回去
	const FTransform Target = Home->GetComponentTransform();
	const float Duration = FVector::Dist(ReturnFrom.GetLocation(), Target.GetLocation()) < 5.f ? 0.12f : ReturnTime;
	ReturnAlpha = FMath::Min(1.f, ReturnAlpha + DeltaSeconds / FMath::Max(0.01f, Duration));
	const float Alpha = FMath::InterpEaseInOut(0.f, 1.f, ReturnAlpha, 2.f);
	SetActorLocationAndRotation(FMath::Lerp(ReturnFrom.GetLocation(), Target.GetLocation(), Alpha),
		FQuat::Slerp(ReturnFrom.GetRotation(), Target.GetRotation(), Alpha));
	if (ReturnAlpha >= 1.f)
	{
		bReturning = false;
		AttachToComponent(Home, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

AToyHand* AToyExplodedModel::GetHolder() const
{
	return Holder.Get();
}

void AToyExplodedModel::AddHover()
{
	++HoverCount;
	RefreshOverlay();
}

void AToyExplodedModel::RemoveHover()
{
	HoverCount = FMath::Max(0, HoverCount - 1);
	RefreshOverlay();
}

void AToyExplodedModel::RefreshOverlay()
{
	UMaterialInterface* Overlay = HoverCount > 0 ? HoverMaterial.Get() : nullptr;
	for (AToyPartUnit* Unit : Units)
	{
		Unit->SetOverlay(Overlay);
	}
}

bool AToyExplodedModel::GetGrabInfo(FText& OutTitle, FText& OutBody) const
{
	OutTitle = ModelTitle;
	OutBody = ModelBody;
	return !ModelTitle.IsEmpty();
}

void AToyExplodedModel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 收起电机时手里可能还拿着零件：先松手，再把单元一起销毁
	ResetAll();
	for (AToyPartUnit* Unit : Units)
	{
		if (IsValid(Unit))
		{
			Unit->Destroy();
		}
	}
	Units.Reset();
	Super::EndPlay(EndPlayReason);
}
