#include "ToyLilliput.h"

#include "ToyShapeComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
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

	/** 变小之前的 r.DepthOfFieldQuality（-1 = 现在是大身体，没改过）。 */
	int32 GLilliSavedDofQuality = -1;

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

	// 人体模特：按顺序找第一个本机有的（都不进仓库，.gitignore 挡着 Content/Characters）：
	// 1. Fab 上的免费女性角色 "Free Rigged Female 3D Character"（用户的素材库里的，导入到 Content/Characters/FabFemale）
	// 2. 引擎模板（Third Person）的 Manny，launch.bat 从引擎目录拷到 Content/Characters/Mannequins
	// Yaw：模特面朝自己的哪边，转多少度面朝 Actor 的 +X。EyeRatio：眼睛在身高的几成（大人像按它放大到和玩家眼睛一样高）
	struct FLilliModel
	{
		const TCHAR* Package;
		const TCHAR* Mesh;
		float Yaw;
		float EyeRatio;
	};
	const FLilliModel LilliModels[] = {
		{ TEXT("/Game/Characters/FabFemale/fbx_clean"), TEXT("/Game/Characters/FabFemale/fbx_clean.fbx_clean"),
		  -90.f, 0.93f },
		{ TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		  -90.f, 0.93f },
	};
	constexpr float LilliFigHeight = 14.f;
	// 站姿：两个模特的参考姿势都是两臂平伸（T / A 字），自带的"动画"也只是这个姿势。上臂朝身体放下这么多度，看着像站着
	constexpr float LilliArmDownDegrees = 72.f;

	TAutoConsoleVariable<int32> CVarLilliModel(
		TEXT("toy.LilliModel"),
		0,
		TEXT("小人偶和大人像用哪个人体模特：0 = 自动（有 Fab 的女性角色就用她，没有用 Manny），1 = Manny，2 = 原来的圆柱。下次开局生效"));

	/** 本机有的第一个模特（都没有就是空，显示原来的圆柱和球）。 */
	const FLilliModel* FindLilliModel()
	{
		const int32 Choice = CVarLilliModel.GetValueOnGameThread();
		if (Choice >= 2)
		{
			return nullptr;
		}
		for (int32 Index = Choice; Index < UE_ARRAY_COUNT(LilliModels); ++Index)
		{
			if (FPackageName::DoesPackageExist(LilliModels[Index].Package))
			{
				return &LilliModels[Index];
			}
		}
		return nullptr;
	}

	/** 上臂（名字里带 upperarm、不是 twist 的骨头）绕肩往身体那边转 LilliArmDownDegrees：从平伸变成放在身边。 */
	void LowerLilliArms(UPoseableMeshComponent* Figure, USkeletalMesh* Mesh)
	{
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
		{
			const FString Name = Skeleton.GetBoneName(Bone).ToString().ToLower();
			if (!Name.Contains(TEXT("upperarm")) || Name.Contains(TEXT("twist")))
			{
				continue;
			}
			// 胳膊的方向：上臂到它下面第一根不是 twist 的骨头（前臂）
			int32 Elbow = INDEX_NONE;
			for (int32 Child = Bone + 1; Child < Skeleton.GetNum(); ++Child)
			{
				if (Skeleton.GetParentIndex(Child) == Bone && !Skeleton.GetBoneName(Child).ToString().ToLower().Contains(TEXT("twist")))
				{
					Elbow = Child;
					break;
				}
			}
			if (Elbow == INDEX_NONE)
			{
				continue;
			}
			const FName BoneName = Skeleton.GetBoneName(Bone);
			const FVector Shoulder = Figure->GetBoneLocationByName(BoneName, EBoneSpaces::ComponentSpace);
			const FVector Arm = (Figure->GetBoneLocationByName(Skeleton.GetBoneName(Elbow), EBoneSpaces::ComponentSpace) - Shoulder).GetSafeNormal();
			// 目标方向：朝下，往自己那一侧张开一点、往前一点（这两个模特的胸前是自己的 -Y：往 +Y 推胳膊会跑到背后）
			const float Side = Arm.X >= 0.f ? 1.f : -1.f;
			const float Spread = FMath::DegreesToRadians(90.f - LilliArmDownDegrees);
			const FVector Target = FVector(Side * FMath::Sin(Spread), -0.12f, -FMath::Cos(Spread)).GetSafeNormal();
			if (FVector::DotProduct(Arm, Target) > 0.97f)
			{
				continue;   // 已经差不多是这个方向了
			}
			const FQuat Turn = FQuat::FindBetweenNormals(Arm, Target);
			const FQuat Current = Figure->GetBoneRotationByName(BoneName, EBoneSpaces::ComponentSpace).Quaternion();
			// 前臂：参考姿势里肘部是弯的，上臂放下以后前臂会拐到背后。也对准胳膊的方向，再往前一点（手放在大腿前侧）。
			// 先把前臂现在的方向、朝向读出来再改上臂（改完马上读拿到的还是旧姿势），按上臂转过的角度换算
			FName ElbowName = Skeleton.GetBoneName(Elbow);
			FQuat ElbowNow = Figure->GetBoneRotationByName(ElbowName, EBoneSpaces::ComponentSpace).Quaternion();
			// 手：前臂下面名字带 hand 的骨头（没有就取离肘最远的子骨头；肘旁边还有些辅助骨头，不能随便取第一个）
			const FVector ElbowAt = Figure->GetBoneLocationByName(ElbowName, EBoneSpaces::ComponentSpace);
			FVector Forearm = FVector::ZeroVector;
			float Farthest = 0.f;
			for (int32 Child = Elbow + 1; Child < Skeleton.GetNum(); ++Child)
			{
				if (Skeleton.GetParentIndex(Child) != Elbow)
				{
					continue;
				}
				const FVector Offset = Figure->GetBoneLocationByName(Skeleton.GetBoneName(Child), EBoneSpaces::ComponentSpace) - ElbowAt;
				if (Skeleton.GetBoneName(Child).ToString().ToLower().Contains(TEXT("hand")))
				{
					Forearm = Offset.GetSafeNormal();
					break;
				}
				if (Offset.Size() > Farthest)
				{
					Farthest = Offset.Size();
					Forearm = Offset.GetSafeNormal();
				}
			}
			Figure->SetBoneRotationByName(BoneName, (Turn * Current).Rotator(), EBoneSpaces::ComponentSpace);
			if (!Forearm.IsNearlyZero())
			{
				const FVector ForearmTarget = (Target + FVector(0.f, -0.25f, 0.f)).GetSafeNormal();
				const FQuat ElbowTurn = FQuat::FindBetweenNormals(Turn.RotateVector(Forearm), ForearmTarget);
				Figure->SetBoneRotationByName(ElbowName, (ElbowTurn * Turn * ElbowNow).Rotator(), EBoneSpaces::ComponentSpace);
				UE_LOG(LogTemp, Verbose, TEXT("ToyPoseDbg %s -> %s: arm %s target %s forearm %s turned %s"), *BoneName.ToString(), *ElbowName.ToString(), *Arm.ToString(), *Target.ToString(), *Forearm.ToString(), *Turn.RotateVector(Forearm).ToString());
			}
		}
	}

	/** 把模特摆成脚底在 Figure 的父组件原点（相对位置 FeetLocal）、身高 Height 厘米（Height 为负 = 眼睛高 -Height）。用绝对缩放，父组件的缩放不影响身高。 */
	bool FitLilliMannequin(UPoseableMeshComponent* Figure, const FVector& FeetLocal, float Height)
	{
		const FLilliModel* Model = FindLilliModel();
		USkeletalMesh* Mannequin = Model ? LoadObject<USkeletalMesh>(nullptr, Model->Mesh) : nullptr;
		if (!Figure || !Mannequin)
		{
			return false;
		}
		if (Height < 0.f)
		{
			Height = -Height / Model->EyeRatio;
		}
		Figure->SetSkinnedAssetAndUpdate(Mannequin);
		Figure->SetRelativeRotation(FRotator(0.f, Model->Yaw, 0.f));
		LowerLilliArms(Figure, Mannequin);
		const FBoxSphereBounds Bounds = Mannequin->GetBounds();
		const float MeshHeight = FMath::Max(1.f, static_cast<float>(Bounds.BoxExtent.Z * 2.f));
		const float Scale = Height / MeshHeight;
		const float FeetZ = static_cast<float>(Bounds.Origin.Z - Bounds.BoxExtent.Z);
		Figure->SetUsingAbsoluteScale(true);
		Figure->SetWorldScale3D(FVector(Scale));
		Figure->SetRelativeLocation(FeetLocal);
		// 网格自己的原点不一定在脚底：按缩放后的脚底高度往上抬（这一段在世界尺度里，父组件的缩放要除掉）
		const FVector ParentScale = Figure->GetAttachParent() ? Figure->GetAttachParent()->GetComponentScale() : FVector::OneVector;
		Figure->AddRelativeLocation(FVector(0.f, 0.f, -FeetZ * Scale / FMath::Max(static_cast<float>(ParentScale.Z), KINDA_SMALL_NUMBER)));
		Figure->SetVisibility(true);
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogTemp, Log, TEXT("ToyBox 小人国：人体模特 %s"), Model->Mesh);
		}
		return true;
	}

	UPoseableMeshComponent* CreateLilliFigure(AActor* Owner, USceneComponent* Parent)
	{
		UPoseableMeshComponent* Figure = Owner->CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Figure"));
		Figure->SetupAttachment(Parent);
		Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Figure->SetCastShadow(true);
		Figure->SetVisibility(false);
		return Figure;
	}
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
	// 景深：样板间的后期体积按人的尺度对焦（几米外清楚），小人眼前几厘米的茶几、书全落在焦外，一片模糊（截图对比过，关掉景深就清楚了）。
	// 小身体时关掉，变回大身体时恢复原来的设置
	if (IConsoleVariable* Dof = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality")))
	{
		if (GLilliScale < 1.f && GLilliSavedDofQuality < 0)
		{
			GLilliSavedDofQuality = Dof->GetInt();
			Dof->Set(0, ECVF_SetByCode);
		}
		else if (GLilliScale >= 1.f && GLilliSavedDofQuality >= 0)
		{
			Dof->Set(GLilliSavedDofQuality, ECVF_SetByCode);
			GLilliSavedDofQuality = -1;
		}
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

	Figure = CreateLilliFigure(this, Mesh);

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
	// 有人体模特就只显示模特：脚底对齐圆柱底面（圆柱是 100 单位高、中心在原点），圆柱和球头藏起来，碰撞还是圆柱的
	if (FitLilliMannequin(Figure, FVector(0.f, 0.f, -50.f), LilliFigHeight))
	{
		Mesh->SetHiddenInGame(true);
		Head->SetHiddenInGame(true);
	}
	// 小人偶也得在醒着时才会立住：刚生成时离地面一点点，叫醒让它落稳
	Mesh->WakeRigidBody();
}

void AToyFigurine::AddHover()
{
	Super::AddHover();
	Figure->SetOverlayMaterial(Mesh->GetOverlayMaterial());
}

void AToyFigurine::RemoveHover()
{
	Super::RemoveHover();
	Figure->SetOverlayMaterial(Mesh->GetOverlayMaterial());
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
	Figure = CreateLilliFigure(this, Root);
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
	// 有人体模特：放大到眼睛和玩家一样高，圆柱、球头、鼻子只藏起来，圆柱的碰撞留着挡小人
	const bool bMannequin = FitLilliMannequin(Figure, FVector::ZeroVector, -Eye);
	Body->SetHiddenInGame(bMannequin);
	Head->SetHiddenInGame(bMannequin);
	Nose->SetHiddenInGame(bMannequin);
	SetActorLocationAndRotation(Feet, FRotator(0.f, Yaw, 0.f));
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AToyGiantBody::Hide()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}
