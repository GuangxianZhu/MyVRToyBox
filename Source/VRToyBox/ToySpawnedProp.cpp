#include "ToySpawnedProp.h"

#include "ToyStudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodySetup.h"

// 常量名带 Spawn 前缀：unity 合并编译时别的 .cpp 匿名命名空间里的同名常量会二义
namespace ToySpawnedPropLocal
{
	/** 每一种的样子：素材包网格（软引用，本机没有就用基础形状），基础形状的尺寸（厘米）和颜色，信息牌名字、材料、质量。 */
	struct FSpawnDef
	{
		const TCHAR* Label;          // 菜单按钮上的英文（TextRender 没有中文字形）
		const TCHAR* LibraryMesh;    // 空 = 只用基础形状
		FRotator LibraryRotation;    // 素材网格摆平要转多少
		bool bCylinder;              // 基础形状：圆柱 / 方块
		FVector SizeCm;
		FLinearColor Color;
		const TCHAR* DisplayName;
		const TCHAR* Material;
		float MassKg;
	};

	// 尺寸按小人国搭东西挑的：书 2cm 厚、瓷砖 2cm、杯垫 1cm 都在小身体一步能跨上去的高度（2.5cm）以内，叠起来就是台阶；
	// 木板 60cm 长、1.5cm 厚，一头搭在盒子（21cm）上是 21° 的斜坡
	const FSpawnDef SpawnDefs[] = {
		{ TEXT("BOOK"), TEXT("/Game/ArchViz/Meshes/SM_Book_02.SM_Book_02"), FRotator(90.f, 0.f, 0.f), false,
		  FVector(24.f, 17.f, 2.f), FLinearColor(0.45f, 0.08f, 0.06f), TEXT("书"), TEXT("纸"), 0.5f },
		{ TEXT("BLOCK"), nullptr, FRotator::ZeroRotator, false,
		  FVector(10.f, 10.f, 10.f), FLinearColor(0.75f, 0.5f, 0.22f), TEXT("木头积木"), TEXT("木"), 0.3f },
		{ TEXT("PLANK"), nullptr, FRotator::ZeroRotator, false,
		  FVector(60.f, 12.f, 1.5f), FLinearColor(0.62f, 0.42f, 0.22f), TEXT("木板（斜坡）"), TEXT("木"), 0.6f },
		{ TEXT("BOX"), TEXT("/Game/ArchViz/Meshes/SM_Box_01.SM_Box_01"), FRotator::ZeroRotator, false,
		  FVector(30.f, 20.f, 15.f), FLinearColor(0.6f, 0.45f, 0.28f), TEXT("收纳盒"), TEXT("纸"), 0.8f },
		{ TEXT("CUP"), TEXT("/Game/FabPacks/Classic_Office/Meshes/DecorativeElements/SM_Cup_01.SM_Cup_01"), FRotator::ZeroRotator, true,
		  FVector(8.f, 8.f, 10.f), FLinearColor(0.9f, 0.9f, 0.88f), TEXT("杯子"), TEXT("陶瓷"), 0.25f },
		{ TEXT("TILE"), nullptr, FRotator::ZeroRotator, false,
		  FVector(20.f, 20.f, 2.f), FLinearColor(0.3f, 0.45f, 0.6f), TEXT("瓷砖（台阶）"), TEXT("陶瓷"), 0.4f },
		{ TEXT("COASTER"), nullptr, FRotator::ZeroRotator, true,
		  FVector(10.f, 10.f, 1.f), FLinearColor(0.7f, 0.55f, 0.38f), TEXT("软木杯垫"), TEXT("木"), 0.05f },
	};

	int64 GSpawnSerial = 0;

	/** toy.Spawn <东西> [X Y [Yaw [Pitch [Z]]]]：不写坐标 = 和菜单一样悬在面前；写了工作室坐标 = 放到那里最高的台面上（Z 写了就按 Z 放中心），睡着不动 */
	void SpawnCommand(const TArray<FString>& Args, UWorld* World);

	FAutoConsoleCommandWithWorldAndArgs SpawnCommandRef(TEXT("toy.Spawn"),
		TEXT("摆东西（和菜单 OBJECTS 页一样）：toy.Spawn book|block|plank|box|cup|tile|coaster [X Y [Yaw [Pitch [Z]]]]，坐标是工作室坐标（厘米，站立点为原点，X 朝工作台）。")
		TEXT("toy.Spawn clear 全部删掉。测试搭台阶用：toy.Spawn book 60 -40，toy.Spawn plank 60 0 90 21 12"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnCommand));

	const FSpawnDef& DefOf(EToySpawnItem Item)
	{
		return SpawnDefs[FMath::Clamp(static_cast<int32>(Item), 0, static_cast<int32>(UE_ARRAY_COUNT(SpawnDefs)) - 1)];
	}
}

using namespace ToySpawnedPropLocal;

bool AToySpawnedProp::bLastSpawnRemovedOldest = false;

bool AToySpawnedProp::ParseItem(const FString& Name, EToySpawnItem& OutItem)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SpawnDefs); ++Index)
	{
		if (Name.Equals(SpawnDefs[Index].Label, ESearchCase::IgnoreCase))
		{
			OutItem = static_cast<EToySpawnItem>(Index);
			return true;
		}
	}
	return false;
}

FString AToySpawnedProp::ItemLabel(EToySpawnItem Item)
{
	return DefOf(Item).Label;
}

FTransform AToySpawnedProp::Setup(EToySpawnItem InItem)
{
	Item = InItem;
	const FSpawnDef& Def = DefOf(InItem);
	DisplayName = Def.DisplayName;
	MaterialText = Def.Material;
	MassKg = Def.MassKg;

	// 素材包里的网格（本机有、而且有简单碰撞才用：物理模拟要凸包或盒子）
	UStaticMesh* Library = nullptr;
	if (Def.LibraryMesh && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(Def.LibraryMesh))))
	{
		Library = LoadObject<UStaticMesh>(nullptr, Def.LibraryMesh);
	}
	if (Library && Library->GetBodySetup() && Library->GetBodySetup()->AggGeom.GetElementCount() > 0)
	{
		Mesh->SetStaticMesh(Library);
		return FTransform(Def.LibraryRotation);
	}
	// 基础形状：引擎的方块 / 圆柱（100 单位、中心在原点），缩放到尺寸，基础材质上色
	UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, Def.bCylinder ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Shape);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Color = UMaterialInstanceDynamic::Create(Base, this);
		Color->SetVectorParameterValue(TEXT("Color"), Def.Color);
		Mesh->SetMaterial(0, Color);
	}
	return FTransform(FQuat::Identity, FVector::ZeroVector, Def.SizeCm / 100.f);
}

AToySpawnedProp* AToySpawnedProp::Spawn(UWorld* World, EToySpawnItem Item, const FTransform& Transform, bool bFloating)
{
	if (!World)
	{
		return nullptr;
	}
	// 满了：拿掉最早摆的那个（悬着没人拿的也算）
	bLastSpawnRemovedOldest = false;
	TArray<AToySpawnedProp*> Existing;
	for (TActorIterator<AToySpawnedProp> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->GetHolder())
		{
			Existing.Add(*It);
		}
	}
	if (Existing.Num() >= MaxSpawned)
	{
		Existing.Sort([](const AToySpawnedProp& A, const AToySpawnedProp& B) { return A.Serial < B.Serial; });
		Existing[0]->Destroy();
		bLastSpawnRemovedOldest = true;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AToySpawnedProp* Prop = World->SpawnActorDeferred<AToySpawnedProp>(StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Prop)
	{
		return nullptr;
	}
	Prop->Serial = ++GSpawnSerial;
	// Setup 给出这一种自己的摆法（素材网格摆平的转动、基础形状的缩放），叠到生成的朝向上。
	// 不经过根组件的相对变换传：延迟生成时根组件的相对变换就是生成变换，再读回来会把转动乘两遍（量过：要 21° 变成 42°）；
	// FinishSpawning 之后再明确设一次，位置也不会被掺进别的东西（量过：位置变成 目标 × (1 − 缩放)）
	const FTransform Local = Prop->Setup(Item);
	const FTransform Final(Transform.GetRotation() * Local.GetRotation(), Transform.GetLocation(), Local.GetScale3D());
	Prop->FinishSpawning(Final);
	Prop->SetActorTransform(Final, false, nullptr, ETeleportType::ResetPhysics);
	Prop->bFloating = bFloating;
	if (bFloating)
	{
		// 悬着：关物理（不受重力），一直亮着，等第一次捏起来
		Prop->Mesh->SetSimulatePhysics(false);
		Prop->AddHover();
	}
	const FVector Size = Prop->Mesh->Bounds.BoxExtent * 2.f;
	UE_LOG(LogTemp, Log, TEXT("ToyBox 摆东西：%s（%s，%.0f×%.0f×%.1fcm）在 %s%s"), *ItemLabel(Item), *GetNameSafe(Prop->Mesh->GetStaticMesh()),
		Size.X, Size.Y, Size.Z, *Transform.GetLocation().ToCompactString(), bLastSpawnRemovedOldest ? TEXT("，满 40 个了，拿掉了最早的一个") : TEXT(""));
	return Prop;
}

int32 AToySpawnedProp::ClearAll(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AToySpawnedProp> It(World); It; ++It)
	{
		if (AToyHand* Hand = It->GetHolder())
		{
			continue;   // 正拿在手里的不删（删了手里会空一下、抓取状态乱掉）
		}
		It->Destroy();
		++Count;
	}
	return Count;
}

int32 AToySpawnedProp::CountFloating(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AToySpawnedProp> It(World); It; ++It)
	{
		Count += It->bFloating ? 1 : 0;
	}
	return Count;
}

void AToySpawnedProp::OnGrabbed(AToyHand* Hand)
{
	if (bFloating)
	{
		// 第一次被拿：不再悬着、不再一直亮，松手以后就是普通的物理摆件（基类 OnReleased 打开物理）
		bFloating = false;
		RemoveHover();
	}
	Super::OnGrabbed(Hand);
}

AToySpawnedProp* AToySpawnedProp::SpawnInFrontOfPlayer(UWorld* World, EToySpawnItem Item)
{
	const APlayerCameraManager* Camera = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
	if (!Camera)
	{
		return nullptr;
	}
	// 菜单就在正前方：摆在右前方，不挡着按钮；已经悬着几个了就往右 / 往上错开（每排 3 个）
	const FRotator Yaw(0.f, Camera->GetCameraRotation().Yaw, 0.f);
	const int32 Slot = CountFloating(World) % 6;
	const FVector Location = Camera->GetCameraLocation() + Yaw.RotateVector(FVector(30.f, 22.f + (Slot % 3) * 22.f, -22.f + (Slot / 3) * 20.f));
	return Spawn(World, Item, FTransform(Yaw, Location), true);
}

int32 AToySpawnedProp::CountAll(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AToySpawnedProp> It(World); It; ++It)
	{
		++Count;
	}
	return Count;
}

void ToySpawnedPropLocal::SpawnCommand(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() == 0)
	{
		return;
	}
	if (Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Log, TEXT("ToyBox 摆东西：删掉 %d 个"), AToySpawnedProp::ClearAll(World));
		return;
	}
	EToySpawnItem Item;
	if (!AToySpawnedProp::ParseItem(Args[0], Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox 摆东西：不认识 %s（book / block / plank / box / cup / tile / coaster / clear）"), *Args[0]);
		return;
	}
	AToyStudio* Studio = AToyStudio::Find(World);
	if (Args.Num() < 3 || !Studio)
	{
		AToySpawnedProp::SpawnInFrontOfPlayer(World, Item);
		return;
	}
	const FTransform StudioTransform = Studio->GetActorTransform();
	const float Yaw = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
	const float Pitch = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.f;
	const FRotator Rotation(Pitch, StudioTransform.Rotator().Yaw + Yaw, 0.f);
	const FVector Top = StudioTransform.TransformPosition(FVector(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), 250.f));
	AToySpawnedProp* Prop = AToySpawnedProp::Spawn(World, Item, FTransform(Rotation, Top), false);
	if (!Prop)
	{
		return;
	}
	if (Args.Num() > 5)
	{
		// 指定了中心高度（斜靠的木板）：直接放，物理先冻住（捏起来松手后恢复），不然一碰到盒子边就被挤平，斜坡就没了
		Prop->Mesh->SetSimulatePhysics(false);
		Prop->SetActorLocation(FVector(Top.X, Top.Y, StudioTransform.GetLocation().Z + FCString::Atof(*Args[5])), false, nullptr, ETeleportType::ResetPhysics);
		UE_LOG(LogTemp, Log, TEXT("ToyBox 摆东西：%s 按指定姿态放好，朝向 %s（要的是 %s），中心 %s"), *Args[0], *Prop->GetActorRotation().ToCompactString(), *Rotation.ToCompactString(), *Prop->GetActorLocation().ToCompactString());
		return;
	}
	// 放到正下方最高的面上：包围盒底面贴着台面（叠书就是一本压一本）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToySpawnPlace), false, Prop);
	Params.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(World, 0));
	FHitResult Hit;
	// 按摆件会碰到的东西找台面（静态、动态、物理物体）：地毯那一片有个看不见的阻挡体积，可见性射线穿过它，
	// 东西放在它里面一醒就被挤到旁边去（量过：盒子被挤开 30cm）
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	if (World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.f, 0.f, 400.f), Objects, Params))
	{
		const float Bottom = static_cast<float>(Prop->Mesh->Bounds.Origin.Z - Prop->Mesh->Bounds.BoxExtent.Z);
		Prop->AddActorWorldOffset(FVector(0.f, 0.f, Hit.ImpactPoint.Z + 0.1f - Bottom), false, nullptr, ETeleportType::ResetPhysics);
		// 摆好就让它睡着：地毯边是斜的，醒着会慢慢滑走（量过：20cm 的瓷砖 5 秒滑了 12cm），测试搭的台阶就对不上了。碰到 / 拿起来会醒
		Prop->Mesh->PutRigidBodyToSleep();
		const FVector Center = StudioTransform.InverseTransformPosition(Prop->Mesh->Bounds.Origin);
		UE_LOG(LogTemp, Log, TEXT("ToyBox 摆东西：%s 放在 %s 上，顶面离地 %.1fcm，包围盒中心在工作室坐标 (%.0f, %.0f)"), *Args[0], *GetNameSafe(Hit.GetActor()),
			Prop->Mesh->Bounds.Origin.Z + Prop->Mesh->Bounds.BoxExtent.Z - StudioTransform.GetLocation().Z, Center.X, Center.Y);
	}
}
