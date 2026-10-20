#include "ToyExplodedModel.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	float EaseInOutCubic(float X)
	{
		return X < 0.5f ? 4.f * X * X * X : 1.f - FMath::Pow(-2.f * X + 2.f, 3.f) * 0.5f;
	}

	/** 单个步骤在总进度 T 下走完了多少（0~1）。 */
	float StepProgress(float T, float Start)
	{
		return EaseInOutCubic(FMath::Clamp((T - Start * 0.6f) / 0.4f, 0.f, 1.f));
	}
}

AToyExplodedModel::AToyExplodedModel()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	ModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelRoot"));
	ModelRoot->SetupAttachment(Root);
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

	FBox Bounds(ForceInit);
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

		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, *PartObject->GetStringField(TEXT("name")));
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Mesh);
		// 零件抓取以后再做，现在先不要碰撞
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetupAttachment(ModelRoot);
		Part->RegisterComponent();
		Parts.Add(Part);
		Bounds += Mesh->GetBoundingBox();

		TArray<FExplodeStep>& Steps = PartSteps.AddDefaulted_GetRef();
		const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
		if (PartObject->TryGetArrayField(TEXT("ex"), StepValues))
		{
			for (const TSharedPtr<FJsonValue>& StepValue : *StepValues)
			{
				const TArray<TSharedPtr<FJsonValue>>& N = StepValue->AsArray();
				if (N.Num() >= 4)
				{
					Steps.Add(FExplodeStep{ FVector(N[0]->AsNumber(), N[1]->AsNumber(), N[2]->AsNumber()), static_cast<float>(N[3]->AsNumber()) });
				}
			}
		}
	}

	if (Parts.Num() == 0 || !Bounds.IsValid)
	{
		return false;
	}

	// 按装配状态的最长边缩放；再把模型底面中心移到 Actor 原点，这样 Actor 放在桌面上模型就立在桌面上
	const float Scale = TargetLengthCm / FMath::Max(1.f, static_cast<float>(Bounds.GetSize().GetMax()));
	const FVector Center = Bounds.GetCenter();
	ModelRoot->SetRelativeScale3D(FVector(Scale));
	ModelRoot->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Bounds.Min.Z) * Scale);

	UE_LOG(LogTemp, Log, TEXT("ToyBox: 模型 %d 个零件，原始尺寸 %s cm，缩放 %.2f 倍"),
		Parts.Num(), *Bounds.GetSize().ToString(), Scale);
	ApplyProgress();
	return true;
}

void AToyExplodedModel::SetExploded(bool bInExploded)
{
	bExploded = bInExploded;
}

void AToyExplodedModel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = bExploded ? 1.f : 0.f;
	if (Progress == Target)
	{
		return;
	}
	// 半路反向时从当前位置直接往回走，不会跳
	Progress = FMath::FInterpConstantTo(Progress, Target, DeltaSeconds, 1.f / FMath::Max(0.1f, ExplodeDuration));
	ApplyProgress();
}

void AToyExplodedModel::ApplyProgress()
{
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FVector Offset = FVector::ZeroVector;
		for (const FExplodeStep& Step : PartSteps[Index])
		{
			Offset += Step.Offset * StepProgress(Progress, Step.Start);
		}
		Parts[Index]->SetRelativeLocation(Offset);
	}
}
