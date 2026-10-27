#include "ToyClawToolRig.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 有名字的命名空间：unity build 会把这个文件和 ToyDexHandRig.cpp / ToyExplodedModel.cpp 拼成一个编译单元，
// 匿名命名空间里同名的 PartsFile / LoadJson / ReadVector 会重定义
namespace ClawToolJson
{
	// 抓取器的导入结果（导入抓取器模型.bat）：parts.json 是零件 → 网格清单，rig.json 是关节表
	const TCHAR* PartsFile = TEXT("Models/ClawTool/parts.json");
	const TCHAR* RigFile = TEXT("Models/ClawTool/rig.json");

	TSharedPtr<FJsonObject> LoadJson(const TCHAR* RelativePath)
	{
		const FString Path = FPaths::ProjectContentDir() / RelativePath;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 找不到抓取器文件 %s"), *Path);
			return nullptr;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 抓取器文件格式不对 %s"), *Path);
			return nullptr;
		}
		return Json;
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->Num() == 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}

	bool ReadFloats(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float* Out, int32 Count)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values->Num() != Count)
		{
			return false;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out[Index] = static_cast<float>((*Values)[Index]->AsNumber());
		}
		return true;
	}
}

bool UToyClawToolRig::Build()
{
	if (bBuilt)
	{
		return true;
	}
	using ClawToolJson::ReadFloats;
	using ClawToolJson::ReadVector;
	const TSharedPtr<FJsonObject> Rig = ClawToolJson::LoadJson(ClawToolJson::RigFile);
	const TSharedPtr<FJsonObject> Manifest = ClawToolJson::LoadJson(ClawToolJson::PartsFile);
	const TArray<TSharedPtr<FJsonValue>>* FingerValues = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PartValues = nullptr;
	if (!Rig || !Manifest || !Rig->TryGetArrayField(TEXT("fingers"), FingerValues) || FingerValues->Num() != NumFingers
		|| !Manifest->TryGetArrayField(TEXT("parts"), PartValues)
		|| !ReadFloats(Rig, TEXT("lengths"), Lengths, NumSegments) || !ReadFloats(Rig, TEXT("thickness"), Thickness, NumSegments)
		|| !ReadFloats(Rig, TEXT("open"), OpenAngles, NumSegments) || !ReadFloats(Rig, TEXT("closed"), ClosedAngles, NumSegments))
	{
		UE_LOG(LogTemp, Error, TEXT("ToyBox: 抓取器 rig.json / parts.json 缺字段，抓取器不显示"));
		return false;
	}
	TipRadius = static_cast<float>(Rig->GetNumberField(TEXT("tip_radius")));
	Pinch = ReadVector(Rig, TEXT("pinch"));
	const TSharedPtr<FJsonObject>* GloveObject = nullptr;
	if (Rig->TryGetObjectField(TEXT("glove"), GloveObject))
	{
		const TSharedPtr<FJsonObject>& G = *GloveObject;
		Glove.FingerCurl = static_cast<float>(G->GetNumberField(TEXT("finger_curl")));
		Glove.IndexCurl = static_cast<float>(G->GetNumberField(TEXT("index_curl")));
		Glove.IndexPull = static_cast<float>(G->GetNumberField(TEXT("index_pull")));
		Glove.ThumbCurl = static_cast<float>(G->GetNumberField(TEXT("thumb_curl")));
		Glove.ThumbRootLimit = static_cast<float>(G->GetNumberField(TEXT("thumb_root_limit")));
		Glove.ThumbTarget = ReadVector(G, TEXT("thumb_target"));
		Glove.bValid = true;
	}

	// 网格：parts.json 里每个零件一个，名字 F{k}S{j} 的是手指节，其余（ClawBody）不动
	TMap<FString, UStaticMeshComponent*> ByName;
	for (const TSharedPtr<FJsonValue>& Value : *PartValues)
	{
		const TSharedPtr<FJsonObject> Part = Value->AsObject();
		const FString Name = Part->GetStringField(TEXT("name"));
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Part->GetStringField(TEXT("mesh")));
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: 抓取器缺网格 %s"), *Name);
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner(),
			MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), FName(*(TEXT("Claw_") + Name))));
		Component->SetupAttachment(this);
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Movable);
		// 和手一样是"幽灵"：不碰撞、不挡鼠标射线
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->bSelectable = false;
		Component->RegisterComponent();
		MeshComponents.Add(Component);
		ByName.Add(Name, Component);
	}

	for (int32 K = 0; K < NumFingers; ++K)
	{
		const TSharedPtr<FJsonObject> Object = (*FingerValues)[K]->AsObject();
		FFinger& Finger = Fingers[K];
		Finger.Base = ReadVector(Object, TEXT("base"));
		Finger.Radial = ReadVector(Object, TEXT("radial")).GetSafeNormal();
		Finger.Axis = ReadVector(Object, TEXT("axis")).GetSafeNormal();
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (!Object->TryGetArrayField(TEXT("segments"), Names) || Names->Num() != NumSegments)
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 抓取器 rig.json 第 %d 根手指缺 segments"), K);
			return false;
		}
		FVector Pivots[NumSegments], Dirs[NumSegments], Normals[NumSegments];
		Solve(Finger, 0.f, Pivots, Dirs, Normals);
		for (int32 J = 0; J < NumSegments; ++J)
		{
			UStaticMeshComponent** Found = ByName.Find((*Names)[J]->AsString());
			Finger.Segments[J] = Found ? *Found : nullptr;
			OpenFrames[K][J] = Frame(Pivots[J], Dirs[J], Normals[J], Finger.Axis);
		}
	}
	bBuilt = MeshComponents.Num() > 0;
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 抓取器 %d 个网格，捏合点 %s"), MeshComponents.Num(), *Pinch.ToCompactString());
	return bBuilt;
}

void UToyClawToolRig::SetToolVisible(bool bNewVisible)
{
	for (UStaticMeshComponent* Component : MeshComponents)
	{
		if (Component)
		{
			Component->SetVisibility(bNewVisible);
		}
	}
}

FMatrix UToyClawToolRig::Frame(const FVector& Pivot, const FVector& Dir, const FVector& Normal, const FVector& Axis)
{
	// UE 的矩阵是行向量约定：局部 (1, 0, 0) 乘上去得到第一行
	return FMatrix(FPlane(Dir, 0.f), FPlane(Normal, 0.f), FPlane(Axis, 0.f), FPlane(Pivot, 1.f));
}

FVector UToyClawToolRig::Solve(const FFinger& Finger, float Close, FVector OutPivot[NumSegments], FVector OutDir[NumSegments], FVector OutNormal[NumSegments]) const
{
	FVector Pivot = Finger.Base;
	float Phi = 0.f;
	for (int32 J = 0; J < NumSegments; ++J)
	{
		Phi += FMath::DegreesToRadians(FMath::Lerp(OpenAngles[J], ClosedAngles[J], Close));
		const FVector Dir = FVector::ForwardVector * FMath::Cos(Phi) + Finger.Radial * FMath::Sin(Phi);
		OutPivot[J] = Pivot;
		OutDir[J] = Dir;
		OutNormal[J] = -FVector::ForwardVector * FMath::Sin(Phi) + Finger.Radial * FMath::Cos(Phi);
		Pivot += Dir * Lengths[J];
	}
	return Pivot;
}

void UToyClawToolRig::SetClose(const float InClose[NumFingers])
{
	if (!bBuilt)
	{
		return;
	}
	for (int32 K = 0; K < NumFingers; ++K)
	{
		const FFinger& Finger = Fingers[K];
		FVector Pivots[NumSegments], Dirs[NumSegments], Normals[NumSegments];
		Solve(Finger, FMath::Clamp(InClose[K], 0.f, 1.f), Pivots, Dirs, Normals);
		for (int32 J = 0; J < NumSegments; ++J)
		{
			if (UStaticMeshComponent* Segment = Finger.Segments[J])
			{
				// 网格顶点在张开姿势：先回到这一节的局部坐标，再放到现在的位置（行向量：先左后右）
				const FMatrix Delta = OpenFrames[K][J].InverseFast() * Frame(Pivots[J], Dirs[J], Normals[J], Finger.Axis);
				Segment->SetRelativeTransform(FTransform(Delta));
			}
		}
	}
}

FVector UToyClawToolRig::TipAt(int32 Finger, float Close) const
{
	if (!bBuilt)
	{
		return GetComponentTransform().TransformPosition(Pinch);
	}
	const FFinger& Data = Fingers[FMath::Clamp(Finger, 0, NumFingers - 1)];
	FVector Pivots[NumSegments], Dirs[NumSegments], Normals[NumSegments];
	const FVector End = Solve(Data, FMath::Clamp(Close, 0.f, 1.f), Pivots, Dirs, Normals);
	// 和 build_claw_tool.py 的 tip_core 一样：离末端 0.5cm，往中轴那一面收进去
	const int32 Last = NumSegments - 1;
	const FVector Core = End - Dirs[Last] * 0.5f - Normals[Last] * (Thickness[Last] * 0.5f - TipRadius * 0.5f);
	return GetComponentTransform().TransformPosition(Core);
}
