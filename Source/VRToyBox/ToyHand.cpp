#include "ToyHand.h"

#include "ToyBoxPawn.h"
#include "ToyCube.h"
#include "ToyDexHandRig.h"
#include "ToyGrabbable.h"
#include "ToyHandCalib.h"
#include "ToyMenuPanel.h"
#include "ToySettings.h"
#include "ToyClawToolRig.h"
#include "ToyFeedback.h"
#include "Components/AudioComponent.h"
#include "ToyTouchScreen.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimSequence.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

static TAutoConsoleVariable<int32> CVarFingerContact(
	TEXT("toy.FingerContact"),
	1,
	TEXT("1 = 捏住东西时拇指和食指弯到碰到物体表面就停（默认）；0 = 旧的固定捏合弯曲度"));

static TAutoConsoleVariable<int32> CVarHandModel(
	TEXT("toy.HandModel"),
	-1,
	TEXT("玩家的手用哪个模型：-1 = 机械夹爪（默认），0 = UE VR 模板的 MannyXR 手，1 = 五指灵巧手，2 = 机械夹爪。可以随时切换，只用来对比"));

static TAutoConsoleVariable<float> CVarDexHandScale(
	TEXT("toy.DexHandScale"),
	1.f,
	TEXT("灵巧手当玩家的手时的缩放（1 = 真实尺寸，手腕到中指尖约 18cm）"));

namespace
{
	// 手掌中心放在握把姿态手背一侧 3cm 处（和 VR 校准的旋转支点共用，见 ToyHandCalib），掌心表面在 1.75cm 处
	constexpr float PalmY = ToyHandCalib::PalmY;
	constexpr float PalmThickness = 2.5f;

	// UE VR 模板的 MannyXR 手（Content/XRMannequins，从引擎 Templates/TemplateResources/Standard/XRMannequins 拷来）
	const TCHAR* RightMeshPath = TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_right.SKM_MannyXR_right");
	const TCHAR* LeftMeshPath = TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_left.SKM_MannyXR_left");
	const TCHAR* OpenAnimPath = TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_Idle_Right.A_MannequinsXR_Idle_Right");
	// 模板的 Grasp 只弯中指、无名指、小指（食指伸直 = 指的手势），食指弯曲另有 IndexCurl，两个合起来才是握拳
	const TCHAR* FistAnimPath = TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_Grasp_Right.A_MannequinsXR_Grasp_Right");
	const TCHAR* IndexCurlAnimPath = TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_IndexCurl_Right.A_MannequinsXR_IndexCurl_Right");
	// 拇指：模板里没有"拇指扣下"的动画，只有"竖大拇指"（ThumbUp）。把 ThumbUp 相对张开姿势的转动反过来用，拇指就往掌心扣
	const TCHAR* ThumbUpAnimPath = TEXT("/Game/XRMannequins/Animations/A_MannequinsXR_ThumbUp_Right.A_MannequinsXR_ThumbUp_Right");

	/** 现在要用的手模型：0 = MannyXR，1 = 灵巧手，2 = 机械夹爪（默认）。控制台 toy.HandModel 设了 0 / 1 就听它的。 */
	int32 WantedHandModel()
	{
		const int32 Override = CVarHandModel.GetValueOnGameThread();
		return Override == 0 || Override == 1 ? Override : 2;
	}

	// 三指抓取器模式下，MannyXR 手当成握着握柄的手套：姿势固定（握拳，拇指搭在头顶的按钮上），
	// 材质用抓取器机身的白色（引擎基础形状材质带噪点纹理，手腕截面看着像一团灰斑，所以不用它）
	constexpr float GloveIndexCurl = 1.f;
	constexpr float GloveFingerCurl = 1.f;
	constexpr float GloveThumbCurl = 0.35f;
	const TCHAR* GloveMaterialPath = TEXT("/Game/Models/ClawTool/claw_tool/Materials/M_ClawWhite.M_ClawWhite");

	// MannyXR 手扳机按满时食指 / 拇指的弯曲程度（两个指尖刚好碰上）。灵巧手的弯曲程度 1 就是捏合手势，见 SetupDexHand
	constexpr float MannyPinchIndexCurl = 0.45f;
	constexpr float MannyPinchThumbCurl = 0.55f;
	constexpr float DexPinchOtherCurl = 0.3f;

	// 信息牌：Slate 像素 → 厘米。560 像素宽 ≈ 22cm，手边看正合适
	constexpr float LabelScale = 0.04f;
	constexpr float LabelWidthPx = 560.f;
	constexpr float LabelBoost = 12.f;

	// 范围小球：引擎自带的球（半径 50cm），材质用零件虚影那个半透明发光材质（参数 Color、Opacity）
	const TCHAR* SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* RangeMaterialPath = TEXT("/Game/Materials/M_ToyGhost.M_ToyGhost");
	constexpr float SphereMeshRadius = 50.f;
	constexpr float RangeBoost = 12.f;

	IToyGrabbable* AsGrabbable(AActor* Actor)
	{
		return Cast<IToyGrabbable>(Actor);
	}

	/** 骨骼名 → 手指组：1 = 食指，2 = 中指 / 无名指 / 小指，3 = 拇指，0 = 手腕、掌骨等不动的 */
	uint8 FingerGroupOf(const FString& Bone)
	{
		if (Bone.StartsWith(TEXT("index")))
		{
			return Bone.Contains(TEXT("metacarpal")) ? 0 : 1;
		}
		if (Bone.StartsWith(TEXT("middle")) || Bone.StartsWith(TEXT("ring")) || Bone.StartsWith(TEXT("pinky")))
		{
			return Bone.Contains(TEXT("metacarpal")) ? 0 : 2;
		}
		if (Bone.StartsWith(TEXT("thumb")))
		{
			return 3;
		}
		return 0;
	}

	/**
	 * 左手网格是右手的镜像，但骨骼局部坐标轴怎么镜像要看建模约定，几种常见的都试一下：
	 * 用右手参考姿势按某种规则镜像后和左手参考姿势比，误差最小的那种就是这套骨骼的规则，
	 * 右手动画（模板只带了右手的）按同样规则镜像就能用在左手上。
	 */
	FTransform MirrorBone(const FTransform& T, int32 Mode)
	{
		const FQuat Q = T.GetRotation();
		const FVector P = T.GetTranslation();
		switch (Mode)
		{
		case 0: return FTransform(FQuat(Q.X, -Q.Y, -Q.Z, Q.W), FVector(-P.X, P.Y, P.Z), T.GetScale3D());
		case 1: return FTransform(FQuat(-Q.X, Q.Y, -Q.Z, Q.W), FVector(P.X, -P.Y, P.Z), T.GetScale3D());
		case 2: return FTransform(FQuat(-Q.X, -Q.Y, Q.Z, Q.W), FVector(P.X, P.Y, -P.Z), T.GetScale3D());
		case 3: return FTransform(Q, -P, T.GetScale3D());
		default: return T;
		}
	}

	float BoneDifference(const FTransform& A, const FTransform& B)
	{
		return (1.f - FMath::Abs(static_cast<float>(A.GetRotation() | B.GetRotation()))) * 10.f
			+ static_cast<float>(FVector::Dist(A.GetTranslation(), B.GetTranslation()));
	}

	FString OtherSideName(const FString& Bone)
	{
		if (Bone.EndsWith(TEXT("_l"))) return Bone.LeftChop(2) + TEXT("_r");
		if (Bone.EndsWith(TEXT("_r"))) return Bone.LeftChop(2) + TEXT("_l");
		return Bone;
	}

	/** 从动画里取一根骨骼在某个时刻的局部变换；动画里没有这根骨骼就返回 false。 */
	bool SampleBone(const UAnimSequence* Anim, const FName Bone, double Time, FTransform& Out)
	{
		const USkeleton* Skeleton = Anim ? Anim->GetSkeleton() : nullptr;
		if (!Skeleton)
		{
			return false;
		}
		const int32 SkeletonIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(Bone);
		if (SkeletonIndex == INDEX_NONE)
		{
			return false;
		}
		const FAnimExtractContext Context(Time);
		Anim->GetBoneTransform(Out, FSkeletonPoseBoneIndex(SkeletonIndex), Context, false);
		return true;
	}

	/** 由局部变换逐级累乘出每根骨骼的组件空间变换（父骨骼的序号总比子骨骼小）。 */
	TArray<FTransform> ComponentSpace(const FReferenceSkeleton& Ref, const TArray<FTransform>& Local)
	{
		TArray<FTransform> Result;
		Result.SetNum(Local.Num());
		for (int32 Index = 0; Index < Local.Num(); ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			Result[Index] = Parent == INDEX_NONE ? Local[Index] : Local[Index] * Result[Parent];
		}
		return Result;
	}
}

AToyHand::AToyHand()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RightFinder(RightMeshPath);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> LeftFinder(LeftMeshPath);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> OpenFinder(OpenAnimPath);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> FistFinder(FistAnimPath);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IndexCurlFinder(IndexCurlAnimPath);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> ThumbUpFinder(ThumbUpAnimPath);
	RightMesh = RightFinder.Object;
	LeftMesh = LeftFinder.Object;
	OpenAnim = OpenFinder.Object;
	FistAnim = FistFinder.Object;
	IndexCurlAnim = IndexCurlFinder.Object;
	ThumbUpAnim = ThumbUpFinder.Object;

	// 手是"幽灵"：不推开方块，也不挡桌面模式的鼠标射线
	HandMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("HandMesh"));
	HandMesh->SetupAttachment(Root);
	HandMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// 五指灵巧手（toy.HandModel 1，默认）：部件网格在 InitHand 里按左右手生成
	DexHand = CreateDefaultSubobject<UToyDexHandRig>(TEXT("DexHand"));
	DexHand->SetupAttachment(Root);

	// 三指抓取器（默认）：网格在 InitHand 里按 rig.json 生成，左手由 SetupClaw 沿 Y 镜像
	ClawTool = CreateDefaultSubobject<UToyClawToolRig>(TEXT("ClawTool"));
	ClawTool->SetupAttachment(Root);

	// 食指指尖：每帧对到骨骼上（见 ApplyFingerPose）
	PokeTip = CreateDefaultSubobject<USceneComponent>(TEXT("PokeTip"));
	PokeTip->SetupAttachment(Root);

	GripPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("GripPoint"));
	GripPoint->SetupAttachment(Root);
	GripPoint->ArrowColor = FColor(0, 220, 255);
	GripPoint->ArrowSize = 0.1f;
	GripPoint->SetHiddenInGame(true);

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

	// 捏合范围小球：每帧挪到捏合点，不碰撞、不投影、不挡鼠标射线
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(SphereMeshPath);
	RangeSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RangeSphere"));
	RangeSphere->SetupAttachment(Root);
	RangeSphere->SetStaticMesh(SphereFinder.Object);
	RangeSphere->SetUsingAbsoluteScale(true);
	RangeSphere->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RangeSphere->SetCastShadow(false);
	RangeSphere->SetReceivesDecals(false);
	RangeSphere->bSelectable = false;
}

void AToyHand::InitHand(AToyBoxPawn* InPawn, EControllerHand InSide)
{
	OwnerPawn = InPawn;
	Side = InSide;

	const float Mirror = (Side == EControllerHand::Left) ? -1.f : 1.f;

	// 掌心表面在 Y = PalmY - 厚度/2 处；X 轴从掌心指向手背（右手 +Y，左手 -Y），Z 轴沿手指（+X）
	const FVector PalmSurface(1.f, Mirror * (PalmY - PalmThickness * 0.5f), 0.f);
	const FRotator GripRotation = FRotationMatrix::MakeFromXZ(FVector(0.f, Mirror, 0.f), FVector::ForwardVector).Rotator();
	GripPoint->SetRelativeLocationAndRotation(PalmSurface, GripRotation);

	SetupHandMesh();

	if (UToyFeedback* System = UToyFeedback::Get(this))
	{
		ServoLoop = System->CreateServoLoop(Root);
	}

	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, RangeMaterialPath))
	{
		RangeMaterial = UMaterialInstanceDynamic::Create(Base, this);
		RangeSphere->SetMaterial(0, RangeMaterial);
	}
	else
	{
		RangeSphere->SetHiddenInGame(true);
	}

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

void AToyHand::SetupHandMesh()
{
	AppliedHandModel = WantedHandModel();
	AppliedDexScale = CVarDexHandScale.GetValueOnGameThread();
	bUseClaw = false;
	SetClawVisible(false);
	HandMesh->EmptyOverrideMaterials();
	if (AppliedHandModel == 1 && SetupDexHand())
	{
		return;
	}
	// 抓取器模式也先把 MannyXR 手摆好（它当握着握柄的手套），再换上抓取器
	SetupMannyHand();
	if (AppliedHandModel == 2)
	{
		SetupClaw();
	}
}

void AToyHand::SetupMannyHand()
{
	bUseDex = false;
	DexHand->SetHandVisible(false);
	HandMesh->SetVisibility(true);
	PinchIndexCurl = MannyPinchIndexCurl;
	PinchThumbCurl = MannyPinchThumbCurl;

	const bool bLeft = Side == EControllerHand::Left;
	const float Mirror = bLeft ? -1.f : 1.f;
	USkeletalMesh* Mesh = bLeft ? LeftMesh : RightMesh;
	if (!Mesh || !RightMesh || !OpenAnim || !FistAnim || !IndexCurlAnim)
	{
		UE_LOG(LogTemp, Error, TEXT("ToyBox: 找不到 MannyXR 手的网格或动画（Content/XRMannequins），手不显示"));
		return;
	}
	HandMesh->SetSkinnedAssetAndUpdate(Mesh);

	// 1) 每根骨骼的"张开 / 握拳"局部变换：右手直接从模板的 Idle / Grasp 动画里取；
	//    左手取右手同名骨骼，按这套骨骼的镜像规则换算（规则用两只手的参考姿势比出来）
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const FReferenceSkeleton& RightRef = RightMesh->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();
	int32 MirrorMode = 4;
	if (bLeft)
	{
		float BestError = TNumericLimits<float>::Max();
		for (int32 Mode = 0; Mode < 5; ++Mode)
		{
			float Error = 0.f;
			for (int32 Index = 0; Index < NumBones; ++Index)
			{
				const FString Name = Ref.GetBoneName(Index).ToString();
				const int32 RightIndex = RightRef.FindBoneIndex(*OtherSideName(Name));
				if (RightIndex != INDEX_NONE && FingerGroupOf(Name) != 0)
				{
					Error += BoneDifference(Ref.GetRefBonePose()[Index], MirrorBone(RightRef.GetRefBonePose()[RightIndex], Mode));
				}
			}
			if (Error < BestError)
			{
				BestError = Error;
				MirrorMode = Mode;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox: 左手骨骼镜像规则 %d，手指参考姿势总误差 %.4f"), MirrorMode, BestError);
	}

	OpenPose = Ref.GetRefBonePose();
	FistPose = Ref.GetRefBonePose();
	BoneGroup.Init(0, NumBones);
	int32 Sampled = 0;
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FString Name = Ref.GetBoneName(Index).ToString();
		BoneGroup[Index] = FingerGroupOf(Name);
		if (BoneGroup[Index] == 0)
		{
			continue;
		}
		const FName AnimBone = bLeft ? FName(*OtherSideName(Name)) : FName(*Name);
		// "握拳"姿势：Grasp 和 IndexCurl 两个动画的最后一帧里，哪个把这根骨骼弯得最多就用哪个
		FTransform Open;
		FTransform Fist;
		float BestBend = -1.f;
		if (SampleBone(OpenAnim, AnimBone, 0.0, Open))
		{
			for (const UAnimSequence* Anim : { FistAnim.Get(), IndexCurlAnim.Get() })
			{
				FTransform Candidate;
				if (SampleBone(Anim, AnimBone, Anim->GetPlayLength(), Candidate))
				{
					const float Bend = static_cast<float>(Open.GetRotation().AngularDistance(Candidate.GetRotation()));
					if (Bend > BestBend)
					{
						BestBend = Bend;
						Fist = Candidate;
					}
				}
			}
		}
		FTransform Up;
		if (BoneGroup[Index] == 3 && ThumbUpAnim && SampleBone(ThumbUpAnim, AnimBone, ThumbUpAnim->GetPlayLength(), Up))
		{
			// 张开 → 竖起 的局部转动是 Delta = Open⁻¹ · Up；扣下 = Open · Delta⁻¹ 再打个 0.8 折，免得扣过头
			const FQuat Delta = Open.GetRotation().Inverse() * Up.GetRotation();
			const FQuat Down = Open.GetRotation() * FQuat::Slerp(FQuat::Identity, Delta.Inverse(), 0.8f);
			Fist = Open;
			Fist.SetRotation(Down);
			BestBend = 1.f;
		}
		if (BestBend >= 0.f)
		{
			OpenPose[Index] = bLeft ? MirrorBone(Open, MirrorMode) : Open;
			FistPose[Index] = bLeft ? MirrorBone(Fist, MirrorMode) : Fist;
			++Sampled;
		}
	}

	const FString Suffix = bLeft ? TEXT("_l") : TEXT("_r");
	auto Find = [&Ref, &Suffix](const TCHAR* Base) { return Ref.FindBoneIndex(*(FString(Base) + Suffix)); };
	IndexTipBone = Find(TEXT("index_03"));
	IndexMidBone = Find(TEXT("index_02"));
	ThumbTipBone = Find(TEXT("thumb_03"));
	ThumbMidBone = Find(TEXT("thumb_02"));
	const int32 WristBone = Find(TEXT("hand"));
	const int32 MiddleBone = Find(TEXT("middle_01"));
	const int32 MiddleTip = Find(TEXT("middle_03"));
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手 MannyXR，%d 根骨骼，%d 根手指骨骼从动画取到张开/握拳姿势"),
		bLeft ? TEXT("左") : TEXT("右"), NumBones, Sampled);
	if (WristBone == INDEX_NONE || MiddleBone == INDEX_NONE || MiddleTip == INDEX_NONE || IndexTipBone == INDEX_NONE || ThumbTipBone == INDEX_NONE)
	{
		FString Names;
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			Names += Ref.GetBoneName(Index).ToString() + TEXT(" ");
		}
		UE_LOG(LogTemp, Error, TEXT("ToyBox: MannyXR 手的骨骼名和预期不一样，手不显示。骨骼：%s"), *Names);
		HandMesh->SetSkinnedAssetAndUpdate(nullptr);
		return;
	}

	// 2) 网格怎么摆：手腕→中指根 = 手指方向（我们的 +X）；握拳时中指尖移动的方向 = 掌心方向（右手 -Y，左手 +Y）。
	//    两个方向都从骨骼算，不依赖网格本身的坐标轴约定
	const TArray<FTransform> OpenCS = ComponentSpace(Ref, OpenPose);
	const TArray<FTransform> FistCS = ComponentSpace(Ref, FistPose);
	const FVector Wrist = OpenCS[WristBone].GetLocation();
	const FVector Knuckle = OpenCS[MiddleBone].GetLocation();
	const FVector FingerDir = (Knuckle - Wrist).GetSafeNormal();
	FVector PalmDir = FistCS[MiddleTip].GetLocation() - OpenCS[MiddleTip].GetLocation();
	PalmDir = (PalmDir - FingerDir * (PalmDir | FingerDir)).GetSafeNormal();

	// 网格空间里"我们的手坐标系"的朝向（X = 手指，Y = 手背方向），取逆就是网格相对手的旋转
	const FQuat HandInMesh = FRotationMatrix::MakeFromXY(FingerDir, -PalmDir * Mirror).ToQuat();
	const FQuat MeshInHand = HandInMesh.Inverse();
	// 手掌中心（手腕和中指根的中点）放到 (0, ±PalmY, 0)
	const FVector PalmCenter = (Wrist + Knuckle) * 0.5f;
	const FVector Target(0.f, Mirror * PalmY, 0.f);
	HandMesh->SetRelativeLocationAndRotation(Target - MeshInHand.RotateVector(PalmCenter), MeshInHand);
	WristBoneIndex = WristBone;
	MiddleBoneIndex = MiddleBone;

	// 捏合点锚定：摆一次"扳机按满"的手势，量出拇指尖和食指尖的中点（手 Actor 坐标系），以后就用这个固定点
	IndexCurl = PinchIndexCurl;
	ThumbCurl = PinchThumbCurl;
	ApplyFingerPose();
	const FVector PinchWorld = (GetFingerTip(ThumbTipBone, ThumbMidBone) + GetFingerTip(IndexTipBone, IndexMidBone)) * 0.5f;
	PinchAnchor = GetActorTransform().InverseTransformPosition(PinchWorld);
	bHasPinchAnchor = true;
	IndexCurl = 0.f;
	ThumbCurl = 0.f;
	ApplyFingerPose();
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手捏合点锚定在手坐标 %s"), bLeft ? TEXT("左") : TEXT("右"), *PinchAnchor.ToCompactString());
}

bool AToyHand::SetupDexHand()
{
	const bool bLeft = Side == EControllerHand::Left;
	const float Mirror = bLeft ? -1.f : 1.f;
	if (!DexHand->Build(bLeft))
	{
		return false;
	}
	bUseDex = true;
	// MannyXR 网格清掉（不只是藏起来）：依赖骨骼的代码（ApplyFingerPose 的骨骼分支等）看到没有骨骼资产就跳过
	HandMesh->SetSkinnedAssetAndUpdate(nullptr);
	HandMesh->SetVisibility(false);
	DexHand->SetHandVisible(true);
	// 灵巧手的弯曲程度 1 = 关节表里的 pinch 手势，两个指腹正好碰上
	PinchIndexCurl = 1.f;
	PinchThumbCurl = 1.f;

	// 和 MannyXR 同样的摆法：手腕 → 中指根 = 手指方向（+X）；手心（模型 +Y）朝 -Y（左手 +Y）；
	// 手掌中心（手腕和中指根的中点，厚度取掌心中线）放到 (0, ±PalmY, 0)。VR 里再由 ToyHandCalib 绕掌心转到握把姿态上
	const float Scale = FMath::Clamp(AppliedDexScale, 0.3f, 3.f);
	const FVector Wrist = DexHand->GetWristModel();
	const FVector Knuckle = DexHand->GetKnuckleModel();
	const FVector FingerDir = (Knuckle - Wrist).GetSafeNormal();
	FVector PalmDir = DexHand->GetPalmDirModel();
	PalmDir = (PalmDir - FingerDir * (PalmDir | FingerDir)).GetSafeNormal();
	const FQuat HandInMesh = FRotationMatrix::MakeFromXY(FingerDir, -PalmDir * Mirror).ToQuat();
	const FQuat MeshInHand = HandInMesh.Inverse();
	FVector PalmCenter = (Wrist + Knuckle) * 0.5f;
	PalmCenter.Y = DexHand->GetPalmCenterY();
	const FVector Target(0.f, Mirror * PalmY, 0.f);
	DexHand->SetRelativeTransform(FTransform(MeshInHand, Target - MeshInHand.RotateVector(PalmCenter * Scale), FVector(Scale)));

	// 捏合点锚定：pinch 手势下拇指指腹和食指指腹的中点（两个指腹在这里碰上）
	const FVector PinchWorld = (DexHand->TipAtCurls(UToyDexHandRig::Thumb, UToyDexHandRig::Pad, 1.f, 1.f, 0.f)
		+ DexHand->TipAtCurls(UToyDexHandRig::Index, UToyDexHandRig::Pad, 1.f, 1.f, 0.f)) * 0.5f;
	PinchAnchor = GetActorTransform().InverseTransformPosition(PinchWorld);
	bHasPinchAnchor = true;
	IndexCurl = 0.f;
	ThumbCurl = 0.f;
	ApplyFingerPose();
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手用五指灵巧手，缩放 %.2f，捏合点锚定在手坐标 %s"),
		bLeft ? TEXT("左") : TEXT("右"), Scale, *PinchAnchor.ToCompactString());
	return true;
}

void AToyHand::SetupClaw()
{
	if (!ClawTool->Build())
	{
		UE_LOG(LogTemp, Error, TEXT("ToyBox: 抓取器模型没导入（Content/Models/ClawTool，跑 导入抓取器模型.bat），玩家的手退回 MannyXR"));
		return;
	}
	bUseClaw = true;
	const float Mirror = (Side == EControllerHand::Left) ? -1.f : 1.f;
	// 模型按右手建的（护手环在手背 +Y 一侧），左手整体沿 Y 镜像
	ClawTool->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.f, Mirror, 1.f)));
	SetClawVisible(true);

	// MannyXR 手当手套：所有材质槽换成机身的白色，姿势在 ApplyFingerPose 里固定成握住握柄
	UMaterialInterface* Glove = LoadObject<UMaterialInterface>(nullptr, GloveMaterialPath);
	if (HandMesh->GetSkinnedAsset() && Glove)
	{
		for (int32 Slot = 0; Slot < HandMesh->GetNumMaterials(); ++Slot)
		{
			HandMesh->SetMaterial(Slot, Glove);
		}
	}

	// 三根手指的合拢程度：1 = 三个指尖在中心碰上
	PinchIndexCurl = 1.f;
	PinchThumbCurl = 1.f;
	PinchAnchor = ClawTool->GetRelativeTransform().TransformPosition(ClawTool->GetPinchLocal());
	bHasPinchAnchor = true;
	for (int32 K = 0; K < UToyClawToolRig::NumFingers; ++K)
	{
		ClawClose[K] = 0.f;
		ClawContact[K] = 1.f;
	}
	ApplyFingerPose();
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手用三指抓取器，捏合点锚定在手坐标 %s"),
		Side == EControllerHand::Left ? TEXT("左") : TEXT("右"), *PinchAnchor.ToCompactString());
}

void AToyHand::SetClawVisible(bool bVisible)
{
	ClawTool->SetToolVisible(bVisible);
}

float AToyHand::FindClawContact(int32 Finger) const
{
	AActor* Target = HeldActor.Get();
	if (!Target)
	{
		return 1.f;
	}
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Target);
	Meshes.RemoveAll([](const UStaticMeshComponent* Mesh) { return !Mesh || !Mesh->IsVisible() || !Mesh->IsCollisionEnabled(); });
	// 和 FindContactCurl 一样：指尖从张开合到底的路径切成 16 段，指尖内侧当成小球沿路径扫，第一次碰到零件就停在那里
	constexpr int32 Steps = 16;
	const FCollisionShape Ball = FCollisionShape::MakeSphere(ClawTool->GetTipRadius());
	FVector From = ClawTool->TipAt(Finger, 0.f);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Close = static_cast<float>(Step) / Steps;
		const FVector To = ClawTool->TipAt(Finger, Close);
		float BestTime = 2.f;
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			for (const bool bComplex : { true, false })
			{
				FHitResult Hit;
				if (Mesh->SweepComponent(Hit, From, To, FQuat::Identity, Ball, bComplex) && !Hit.bStartPenetrating)
				{
					BestTime = FMath::Min(BestTime, Hit.Time);
				}
			}
		}
		if (BestTime <= 1.f)
		{
			return (Step - 1 + BestTime) / Steps;
		}
		From = To;
	}
	return 1.f;
}

FVector AToyHand::GetFingerDirectionLocal() const
{
	if (bUseClaw)
	{
		return FVector::ForwardVector;
	}
	if (bUseDex)
	{
		const FTransform& Mesh = DexHand->GetComponentTransform();
		const FVector Dir = Mesh.TransformPosition(DexHand->GetKnuckleModel()) - Mesh.TransformPosition(DexHand->GetWristModel());
		return GetActorTransform().InverseTransformVectorNoScale(Dir).GetSafeNormal();
	}
	if (!HandMesh->GetSkinnedAsset() || WristBoneIndex == INDEX_NONE || MiddleBoneIndex == INDEX_NONE)
	{
		return FVector::ForwardVector;
	}
	const FVector Wrist = HandMesh->GetBoneLocation(HandMesh->GetBoneName(WristBoneIndex));
	const FVector Knuckle = HandMesh->GetBoneLocation(HandMesh->GetBoneName(MiddleBoneIndex));
	return GetActorTransform().InverseTransformVectorNoScale(Knuckle - Wrist).GetSafeNormal();
}

void AToyHand::ShowCustomInfo(const FText& Title, const FText& Body)
{
	if (!InfoTitle.IsValid())
	{
		return;
	}
	bCustomInfo = true;
	InfoTitle->SetText(Title);
	InfoBody->SetText(Body);
	InfoLabel->SetHiddenInGame(false);
}

void AToyHand::HideCustomInfo()
{
	if (!bCustomInfo)
	{
		return;
	}
	bCustomInfo = false;
	ShowInfo(HeldActor.Get());
}

FVector AToyHand::GetFingerTip(int32 TipBone, int32 PrevBone) const
{
	if (!HandMesh->GetSkinnedAsset() || TipBone == INDEX_NONE || PrevBone == INDEX_NONE)
	{
		return GripPoint->GetComponentLocation() + GripPoint->GetUpVector() * 9.f;
	}
	// 末节骨骼原点在指节上，指尖再往外延伸差不多一节末节的长度
	const FVector Tip = HandMesh->GetBoneLocation(HandMesh->GetBoneName(TipBone));
	const FVector Prev = HandMesh->GetBoneLocation(HandMesh->GetBoneName(PrevBone));
	return Tip + (Tip - Prev) * 0.85f;
}

void AToyHand::ApplyFingerPose()
{
	if (bUseClaw)
	{
		// 手套手固定握住握柄；三根手指各按自己的合拢程度摆。戳触屏用捏合点（三根手指中间，张开时指尖也差不多在这个深度）
		PoseManny(GloveIndexCurl, GloveFingerCurl, GloveThumbCurl);
		ClawTool->SetClose(ClawClose);
		PokeTip->SetWorldLocation(GetPinchLocation());
		return;
	}
	if (bUseDex)
	{
		// 灵巧手：食指、拇指按扳机往捏合手势弯，其余三指按握把往握拳弯（关节角线性插值，见 UToyDexHandRig）
		// 捏的时候中指、无名指、小指跟着微微弯一点（握拳的 30%），不然直挺挺的像比"OK"
		DexHand->SetCurls(IndexCurl, ThumbCurl, FMath::Max(FingerCurl, DexPinchOtherCurl * IndexCurl));
		PokeTip->SetWorldLocation(DexHand->GetTip(UToyDexHandRig::Index, UToyDexHandRig::End));
		return;
	}
	if (!PoseManny(IndexCurl, FingerCurl, ThumbCurl))
	{
		return;
	}
	PokeTip->SetWorldLocation(GetFingerTip(IndexTipBone, IndexMidBone));
}

bool AToyHand::PoseManny(float InIndexCurl, float InFingerCurl, float InThumbCurl)
{
	if (!HandMesh->GetSkinnedAsset() || OpenPose.Num() != HandMesh->BoneSpaceTransforms.Num())
	{
		return false;
	}
	// 每根骨骼在张开和握拳之间按所属手指组的弯曲程度插值（旋转球面插值，位置线性插值）
	const float Weights[4] = { 0.f, InIndexCurl, InFingerCurl, InThumbCurl };
	for (int32 Index = 0; Index < OpenPose.Num(); ++Index)
	{
		FTransform Pose;
		Pose.Blend(OpenPose[Index], FistPose[Index], Weights[BoneGroup[Index]]);
		HandMesh->BoneSpaceTransforms[Index] = Pose;
	}
	HandMesh->MarkRefreshTransformDirty();
	HandMesh->RefreshBoneTransforms();
	return true;
}

void AToyHand::SetInputs(float InGrip, float InTrigger)
{
	Grip = FMath::Clamp(InGrip, 0.f, 1.f);
	Trigger = FMath::Clamp(InTrigger, 0.f, 1.f);
}

void AToyHand::SetDesktopPose(const FVector& PinchWorldLocation, const FRotator& Facing)
{
	// 捏合点随手指弯曲在变，用它当前相对手的位置来摆
	const FQuat Rotation = Facing.Quaternion();
	const FVector PinchLocal = GetActorTransform().InverseTransformPosition(GetPinchLocation());
	SetActorLocationAndRotation(PinchWorldLocation - Rotation.RotateVector(PinchLocal), Rotation);
}

FVector AToyHand::GetPinchLocation() const
{
	if (bHasPinchAnchor)
	{
		return GetActorTransform().TransformPosition(PinchAnchor);
	}
	return (GetFingerTip(ThumbTipBone, ThumbMidBone) + PokeTip->GetComponentLocation()) * 0.5f;
}

void AToyHand::SetPokeEnabled(bool bEnabled)
{
	bPokeEnabled = bEnabled;
	if (!bEnabled)
	{
		Touching.Reset();
		if (AToyTouchScreen* Screen = TouchingScreen.Get())
		{
			Screen->ReleaseFinger(this);
		}
		TouchingScreen.Reset();
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

	// 切换手模型（设置页 HAND MODEL、控制台 toy.HandModel / toy.DexHandScale）：重新摆一次
	if (WantedHandModel() != AppliedHandModel || CVarDexHandScale.GetValueOnGameThread() != AppliedDexScale)
	{
		SetupHandMesh();
	}

	// 抓取只看扳机（拇指食指捏）：按过 PinchThreshold 捏起，松到 ReleaseThreshold 以下放开。握把不参与
	if (!bTriggerDown && Trigger > PinchThreshold)
	{
		bTriggerDown = true;
		if (AActor* Candidate = FindGrabCandidate())
		{
			Grab(Candidate);
		}
	}
	else if (bTriggerDown && Trigger < ReleaseThreshold)
	{
		bTriggerDown = false;
		ReleaseHeld(true);
	}

	// 拿着的东西被别处销毁了（比如收起了电机）：把信息牌也收掉
	if (!HeldActor.IsValid() && !InfoLabel->bHiddenInGame && !bCustomInfo)
	{
		ShowInfo(nullptr);
	}

	// 抓取范围跟着设置走（SMALL / MEDIUM / LARGE），范围小球的大小也跟着变
	GrabRadius = ToySettings::GrabRadiusCm();
	UpdateHover();
	TickHeld(DeltaSeconds);
	TickCurls(DeltaSeconds);
	TickInfo();
	TickRangeSphere(DeltaSeconds);
	if (bPokeEnabled)
	{
		TickPoke();
	}

	GripPoint->SetHiddenInGame(!ToyBox::ShowGrabPoints());
	if (ToyBox::ShowGrabPoints())
	{
		DrawDebugSphere(GetWorld(), GetPinchLocation(), GrabRadius, 12, FColor(255, 120, 0));
	}
}

AActor* AToyHand::FindGrabCandidate() const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyHandGrab), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);   // 方块
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);  // 电机零件 / 整机的检测盒
	// 只在拇指和食指之间判定：捏合点 GrabRadius 以内碰到碰撞表面才算，手掌、手背碰到不算
	const FVector PinchLocation = GetPinchLocation();
	GetWorld()->OverlapMultiByObjectType(Overlaps, PinchLocation, FQuat::Identity, Objects, FCollisionShape::MakeSphere(GrabRadius), Params);

	// 按"捏合点到碰撞体表面的距离"挑最近的；完全一样近（捏合点同时在两个检测盒里面）时挑小的那个
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
		float Distance = Component->GetClosestPointOnCollision(PinchLocation, Closest);
		if (Distance < 0.f)
		{
			Distance = FVector::Dist(Component->Bounds.Origin, PinchLocation);
		}
		const float Size = Component->Bounds.SphereRadius;
		const bool bCloser = Distance < BestDistance - 0.05f;
		const bool bTieButSmaller = FMath::Abs(Distance - BestDistance) <= 0.05f && Size < BestSize;
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
	Context.PinchInGrip = Context.GripWorld.InverseTransformPosition(GetPinchLocation());
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
	for (float& Contact : ClawContact)
	{
		Contact = 1.f;
	}
	for (bool& bTouched : bFingerTouched)
	{
		bTouched = false;
	}
	ContactIndexCurl = PinchIndexCurl;
	ContactThumbCurl = PinchThumbCurl;
	bContactPending = true;
	ShowInfo(Target);
	// 抓取器：等三根手指碰到表面再响（TickContactFeedback）；别的手模型没有这个过程，抓起就锁定
	bSecurePending = bUseClaw;
	if (!bUseClaw)
	{
		Feedback(EToyFeedback::GrabSecured, GetPinchLocation(), Target);
	}
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
	bSecurePending = false;
	const FVector Velocity = bThrow ? GetThrowVelocity() : FVector::ZeroVector;
	Feedback(EToyFeedback::Release, GetPinchLocation(), Target, static_cast<float>(Velocity.Size()));
	if (IToyGrabbable* Grabbable = AsGrabbable(Target))
	{
		Grabbable->OnReleased(this, Velocity);
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
		Feedback(EToyFeedback::HoverEnter, GetPinchLocation(), Candidate);
	}
	// 范围小球跟着变色（见 TickRangeSphere）；VR 里看不清时可以查日志
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手捏合范围内：%s"), Side == EControllerHand::Left ? TEXT("左") : TEXT("右"),
		Candidate ? *Candidate->GetName() : TEXT("（空，小球变回淡白）"));
}

void AToyHand::ShowInfo(AActor* Target)
{
	if (bCustomInfo)
	{
		return;   // 校准模式的牌子占着
	}
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
	if ((!Target && !bCustomInfo) || InfoLabel->bHiddenInGame)
	{
		return;
	}
	// 摆在拿着的东西正上方 6cm（牌子的底边对齐这里），整个牌子转过来正对玩家的眼睛；校准模式的牌子在手上方 12cm
	FVector Anchor = GetActorLocation() + FVector(0.f, 0.f, 12.f);
	if (!bCustomInfo)
	{
		FVector Origin;
		FVector Extent;
		Target->GetActorBounds(true, Origin, Extent);
		Anchor = FVector(Origin.X, Origin.Y, Origin.Z + Extent.Z + 6.f);
	}

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
	// 弯曲程度 0 = 张开（Idle），1 = 握拳（Grasp）。
	// 扳机 → 拇指和食指一起合拢到"捏"的姿势（不按扳机时食指伸直，用来戳触屏）；握把 → 中指、无名指、小指。
	// 捏着东西时拇指食指保持不动，扳机松一点也不张开（真正松手由 Tick 里的迟滞决定）；
	// 而且不是固定弯到"捏合"，是各自弯到指尖碰到物体表面为止（ComputeContactCurls，等吸附滑完再算）
	const bool bHolding = HeldActor.IsValid();
	if (bUseClaw)
	{
		// 抓取器：没拿东西时三根手指跟着扳机一起合拢；拿着东西时各自停在碰到零件的地方（吸附滑完后算一次）
		if (bHolding && bContactPending && SnapAlpha >= 1.f)
		{
			bContactPending = false;
			ComputeContactCurls();
		}
		float Previous[UToyClawToolRig::NumFingers];
		for (int32 K = 0; K < UToyClawToolRig::NumFingers; ++K)
		{
			Previous[K] = ClawClose[K];
			ClawClose[K] = FMath::FInterpTo(ClawClose[K], bHolding ? ClawContact[K] : Trigger, DeltaSeconds, 20.f);
		}
		ApplyFingerPose();
		TickServo(Previous, DeltaSeconds);
		if (bHolding && !bContactPending)
		{
			TickContactFeedback();
		}
		return;
	}
	if (bHolding && bContactPending && SnapAlpha >= 1.f)
	{
		bContactPending = false;
		ComputeContactCurls();
	}
	const float Speed = 20.f;
	FingerCurl = FMath::FInterpTo(FingerCurl, Grip, DeltaSeconds, Speed);
	IndexCurl = FMath::FInterpTo(IndexCurl, bHolding ? ContactIndexCurl : Trigger * PinchIndexCurl, DeltaSeconds, Speed);
	ThumbCurl = FMath::FInterpTo(ThumbCurl, bHolding ? ContactThumbCurl : Trigger * PinchThumbCurl, DeltaSeconds, Speed);
	ApplyFingerPose();
}

FVector AToyHand::FingerTipAtCurl(uint8 Group, float Curl, int32 TipBone, int32 PrevBone) const
{
	if (bUseDex)
	{
		// 灵巧手：关节链正向运动学，球心取指尖套中心（离指腹约 0.7cm，和 FingerTipRadius 差不多，球面正好是指腹）
		const bool bIndex = Group == 1;
		return DexHand->TipAtCurls(bIndex ? UToyDexHandRig::Index : UToyDexHandRig::Thumb, UToyDexHandRig::Core,
			bIndex ? Curl : IndexCurl, bIndex ? ThumbCurl : Curl, FingerCurl);
	}
	// 和 ApplyFingerPose 同样的插值，但只在内存里算骨骼位置，不动网格（试很多个弯曲度时不用每次刷新骨骼）
	const USkinnedAsset* Asset = HandMesh->GetSkinnedAsset();
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const float Weights[4] = { 0.f, Group == 1 ? Curl : IndexCurl, FingerCurl, Group == 3 ? Curl : ThumbCurl };
	TArray<FTransform> Local;
	Local.SetNum(OpenPose.Num());
	for (int32 Index = 0; Index < OpenPose.Num(); ++Index)
	{
		Local[Index].Blend(OpenPose[Index], FistPose[Index], Weights[BoneGroup[Index]]);
	}
	const TArray<FTransform> CS = ComponentSpace(Ref, Local);
	const FVector Tip = CS[TipBone].GetLocation();
	const FVector Prev = CS[PrevBone].GetLocation();
	// 指尖往外延伸的比例和 GetFingerTip 一致
	return HandMesh->GetComponentTransform().TransformPosition(Tip + (Tip - Prev) * 0.85f);
}

float AToyHand::FindContactCurl(uint8 Group, float MaxCurl, int32 TipBone, int32 PrevBone) const
{
	AActor* Target = HeldActor.Get();
	if (!Target || (!bUseDex && (TipBone == INDEX_NONE || PrevBone == INDEX_NONE)))
	{
		return MaxCurl;
	}
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Target);
	Meshes.RemoveAll([](const UStaticMeshComponent* Mesh) { return !Mesh || !Mesh->IsVisible() || !Mesh->IsCollisionEnabled(); });
	if (Meshes.Num() == 0)
	{
		return MaxCurl;
	}

	// 指尖从张开弯到 MaxCurl 的路径切成几小段，把指尖当成小球沿每段扫过去，第一次碰到物体的那一段里按命中比例插出弯曲度
	constexpr int32 Steps = 16;
	const FCollisionShape Ball = FCollisionShape::MakeSphere(FingerTipRadius);
	FVector From = FingerTipAtCurl(Group, 0.f, TipBone, PrevBone);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Curl = MaxCurl * Step / Steps;
		const FVector To = FingerTipAtCurl(Group, Curl, TipBone, PrevBone);
		float BestTime = 2.f;
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			// 零件网格打三角形（复杂碰撞）；方块这类引擎基本形状再打一遍简单碰撞，两者取先碰到的
			for (const bool bComplex : { true, false })
			{
				FHitResult Hit;
				if (Mesh->SweepComponent(Hit, From, To, FQuat::Identity, Ball, bComplex) && !Hit.bStartPenetrating)
				{
					BestTime = FMath::Min(BestTime, Hit.Time);
				}
			}
		}
		if (BestTime <= 1.f)
		{
			return MaxCurl * (Step - 1 + BestTime) / Steps;
		}
		From = To;
	}
	// 一路都没碰到（物体不在指尖路径上）：照旧弯到捏合
	return MaxCurl;
}

void AToyHand::ComputeContactCurls()
{
	const bool bMannyReady = HandMesh->GetSkinnedAsset() && OpenPose.Num() == HandMesh->BoneSpaceTransforms.Num();
	if (bUseClaw)
	{
		// toy.FingerContact 0：三根手指直接合到底
		const bool bContact = CVarFingerContact.GetValueOnGameThread() != 0;
		for (int32 K = 0; K < UToyClawToolRig::NumFingers; ++K)
		{
			ClawContact[K] = bContact ? FindClawContact(K) : 1.f;
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手抓取器夹住 %s，三根手指合拢到 %.2f / %.2f / %.2f"),
			Side == EControllerHand::Left ? TEXT("左") : TEXT("右"), *GetNameSafe(HeldActor.Get()), ClawContact[0], ClawContact[1], ClawContact[2]);
		return;
	}
	if (!CVarFingerContact.GetValueOnGameThread() || !(bUseDex ? DexHand->IsBuilt() : bMannyReady))
	{
		return;
	}
	ContactIndexCurl = FindContactCurl(1, PinchIndexCurl, IndexTipBone, IndexMidBone);
	ContactThumbCurl = FindContactCurl(3, PinchThumbCurl, ThumbTipBone, ThumbMidBone);
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手捏住 %s，食指弯到 %.2f / %.2f，拇指 %.2f / %.2f"),
		Side == EControllerHand::Left ? TEXT("左") : TEXT("右"), *GetNameSafe(HeldActor.Get()),
		ContactIndexCurl, PinchIndexCurl, ContactThumbCurl, PinchThumbCurl);
}

void AToyHand::TickRangeSphere(float DeltaSeconds)
{
	if (!RangeMaterial)
	{
		return;
	}
	// 设置里可以把小球关掉（抓取判定不变，只是不显示）
	RangeSphere->SetHiddenInGame(!ToySettings::Get().bPinchBall);
	// 平时淡白；有东西进范围（零件同时高亮）变绿；捏住后缩到一半、更淡，不挡着看手里的零件
	const bool bHolding = HeldActor.IsValid();
	const bool bHover = !bHolding && HoveredActor.IsValid();
	const FLinearColor Color = bHover ? FLinearColor(0.2f, 1.f, 0.35f) : FLinearColor(1.f, 1.f, 1.f);
	const float Opacity = bHolding ? 0.06f : (bHover ? 0.35f : 0.12f);
	RangeScale = FMath::FInterpTo(RangeScale, bHolding ? 0.5f : 1.f, DeltaSeconds, 15.f);
	RangeMaterial->SetVectorParameterValue(TEXT("Color"), Color * (bHover ? RangeBoost : RangeBoost * 0.25f));
	RangeMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	RangeSphere->SetWorldLocation(GetPinchLocation());
	RangeSphere->SetWorldScale3D(FVector(GrabRadius * RangeScale / SphereMeshRadius));
}

void AToyHand::TickPoke()
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyHandPoke), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, PokeTip->GetComponentLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(1.f), Params);

	// 触屏（GUI）：指尖在它的感应区里就每帧交给它，由它按离屏幕的距离判断悬停 / 按下 / 松开。
	// 主菜单面板（3D 按钮）：只在"刚碰到"按钮触发盒的那一帧触发，手指停在按钮里不会连发
	AToyTouchScreen* ScreenNow = nullptr;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> NowTouching;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		AToyTouchScreen* Screen = Cast<AToyTouchScreen>(Overlap.GetActor());
		if (Screen && Component == Screen->TouchZone.Get() && !ScreenNow)
		{
			// 悬停 / 按下 / 松开的声音和震动由触屏自己发（鼠标点也要响），见 AToyTouchScreen::TouchFinger
			ScreenNow = Screen;
			Screen->TouchFinger(this, PokeTip->GetComponentLocation());
			continue;
		}
		AToyMenuPanel* Menu = Cast<AToyMenuPanel>(Overlap.GetActor());
		if (!Component || !Menu)
		{
			continue;
		}
		const TWeakObjectPtr<UPrimitiveComponent> WeakComponent(Component);
		NowTouching.Add(WeakComponent);
		if (!Touching.Contains(WeakComponent) && Menu->Poke(Component))
		{
			Feedback(EToyFeedback::UiPress, PokeTip->GetComponentLocation(), nullptr);
		}
	}
	// 手指离开菜单按钮：轻一点的"松开"
	for (const TWeakObjectPtr<UPrimitiveComponent>& Previous : Touching)
	{
		if (Previous.IsValid() && !NowTouching.Contains(Previous))
		{
			Feedback(EToyFeedback::UiRelease, PokeTip->GetComponentLocation(), nullptr);
		}
	}
	Touching = MoveTemp(NowTouching);
	if (AToyTouchScreen* Previous = TouchingScreen.Get(); Previous && Previous != ScreenNow)
	{
		Previous->ReleaseFinger(this);
	}
	TouchingScreen = ScreenNow;
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

void AToyHand::Feedback(EToyFeedback Event, const FVector& Location, const AActor* Target, float Speed) const
{
	UToyFeedback* System = UToyFeedback::Get(this);
	if (!System)
	{
		return;
	}
	FToyFeedbackParams Params;
	Params.Location = Location;
	Params.Hand = Side;
	Params.Speed = Speed;
	Params.Source = this;
	if (Target)
	{
		Params.Material = UToyFeedback::MaterialOf(Target);
		Params.SizeCm3 = UToyFeedback::SizeOf(Target);
	}
	System->Play(Event, Params);
}

void AToyHand::TickServo(const float* PreviousClose, float DeltaSeconds)
{
	// 三根手指这一帧一共动了多少（合拢量，0~1 一根手指走完全程是 1）
	float Moved = 0.f;
	for (int32 K = 0; K < UToyClawToolRig::NumFingers; ++K)
	{
		Moved += FMath::Abs(ClawClose[K] - PreviousClose[K]);
	}
	const float Speed = DeltaSeconds > 0.f ? Moved / DeltaSeconds : 0.f;

	// 电机声：动得越快越响、越高；停下就静音。一根手指以 1/秒 走算"慢"
	if (UToyFeedback* System = UToyFeedback::Get(this))
	{
		const float Volume = FMath::Clamp((Speed - 0.15f) / 4.f, 0.f, 1.f) * 0.5f;
		System->SetLoop(ServoLoop, Volume, 0.8f + FMath::Clamp(Speed / 8.f, 0.f, 0.7f));
	}

	// 齿轮感：合拢量每累计 0.08 震一下小的，手指动得快就震得密；同时有一点很弱的持续震
	ServoAccum += Moved / UToyClawToolRig::NumFingers;
	if (ServoAccum >= 0.08f)
	{
		ServoAccum = FMath::Fmod(ServoAccum, 0.08f);
		Feedback(EToyFeedback::ServoTick, GetPinchLocation(), nullptr);
	}
	if (AToyBoxPawn* Pawn = OwnerPawn.Get())
	{
		Pawn->SetHapticDrive(Side, FMath::Clamp(Speed * 0.015f, 0.f, 0.06f));
	}
}

void AToyHand::TickContactFeedback()
{
	const AActor* Target = HeldActor.Get();
	bool bAllInPlace = true;
	for (int32 K = 0; K < UToyClawToolRig::NumFingers; ++K)
	{
		const bool bInPlace = FMath::Abs(ClawClose[K] - ClawContact[K]) < 0.03f;
		bAllInPlace &= bInPlace;
		if (bInPlace && !bFingerTouched[K])
		{
			bFingerTouched[K] = true;
			// 合到底也没碰到零件的手指（ClawContact = 1）不响
			if (ClawContact[K] < 0.995f)
			{
				Feedback(EToyFeedback::FingerTouch, ClawTool->TipAt(K, ClawClose[K]), Target);
			}
		}
	}
	if (bAllInPlace && bSecurePending)
	{
		bSecurePending = false;
		Feedback(EToyFeedback::GrabSecured, GetPinchLocation(), Target);
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
