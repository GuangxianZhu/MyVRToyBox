#include "ToyDexHandRig.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// 灵巧手的导入结果（导入灵巧手模型.bat）：parts.json 是零件 → 网格清单，rig.json 是关节表
	const TCHAR* PartsFile = TEXT("Models/DexHand/parts.json");
	const TCHAR* RigFile = TEXT("Models/DexHand/rig.json");

	const TCHAR* FingerNames[] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("little") };

	TSharedPtr<FJsonObject> LoadJson(const TCHAR* RelativePath)
	{
		const FString Path = FPaths::ProjectContentDir() / RelativePath;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 找不到灵巧手文件 %s"), *Path);
			return nullptr;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 灵巧手文件格式不对 %s"), *Path);
			return nullptr;
		}
		return Json;
	}

	FVector ReadRigVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->Num() == 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}

	bool ReadAngles(const TSharedPtr<FJsonObject>& Poses, const TCHAR* Pose, int32 Finger, float Out[4])
	{
		const TSharedPtr<FJsonObject>* PoseObject = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Poses->TryGetObjectField(Pose, PoseObject) || !(*PoseObject)->TryGetArrayField(FingerNames[Finger], Values) || Values->Num() != 4)
		{
			return false;
		}
		for (int32 K = 0; K < 4; ++K)
		{
			Out[K] = static_cast<float>((*Values)[K]->AsNumber());
		}
		return true;
	}
}

bool UToyDexHandRig::Build(bool bInLeft)
{
	if (bBuilt && bLeft == bInLeft)
	{
		return true;
	}
	const TSharedPtr<FJsonObject> Rig = LoadJson(RigFile);
	const TSharedPtr<FJsonObject> Manifest = LoadJson(PartsFile);
	const TSharedPtr<FJsonObject>* JointsObject = nullptr;
	const TSharedPtr<FJsonObject>* PosesObject = nullptr;
	const TSharedPtr<FJsonObject>* TipsObject = nullptr;
	const TSharedPtr<FJsonObject>* UnitsObject = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PartValues = nullptr;
	if (!Rig || !Manifest || !Rig->TryGetObjectField(TEXT("joints"), JointsObject) || !Rig->TryGetObjectField(TEXT("poses"), PosesObject)
		|| !Rig->TryGetObjectField(TEXT("tips"), TipsObject) || !Rig->TryGetObjectField(TEXT("units"), UnitsObject)
		|| !Manifest->TryGetArrayField(TEXT("parts"), PartValues))
	{
		UE_LOG(LogTemp, Error, TEXT("ToyBox: 灵巧手 rig.json / parts.json 缺字段，玩家的手退回 MannyXR"));
		return false;
	}

	// 左手：模型沿 X 镜像。点 p → S·p；转轴 a → -(S·a)（镜像会把转动方向反过来，轴取反再转回来）
	bLeft = bInLeft;
	const FVector S(bLeft ? -1.f : 1.f, 1.f, 1.f);

	for (int32 Finger = 0; Finger < NumFingers; ++Finger)
	{
		const TArray<TSharedPtr<FJsonValue>>* Chain = nullptr;
		const TSharedPtr<FJsonObject>* Tip = nullptr;
		if (!(*JointsObject)->TryGetArrayField(FingerNames[Finger], Chain) || Chain->Num() != 4
			|| !(*TipsObject)->TryGetObjectField(FingerNames[Finger], Tip)
			|| !ReadAngles(*PosesObject, TEXT("rest"), Finger, RestAngles[Finger])
			|| !ReadAngles(*PosesObject, TEXT("pinch"), Finger, PinchAngles[Finger])
			|| !ReadAngles(*PosesObject, TEXT("fist"), Finger, FistAngles[Finger]))
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: 灵巧手 rig.json 里 %s 的数据不全，玩家的手退回 MannyXR"), FingerNames[Finger]);
			return false;
		}
		for (int32 K = 0; K < 4; ++K)
		{
			const TSharedPtr<FJsonObject> Joint = (*Chain)[K]->AsObject();
			Joints[Finger][K].Pivot = ReadRigVector(Joint, TEXT("pivot")) * S;
			Joints[Finger][K].Axis = bLeft ? -(ReadRigVector(Joint, TEXT("axis")) * S).GetSafeNormal() : ReadRigVector(Joint, TEXT("axis")).GetSafeNormal();
		}
		Tips[Finger][Pad] = ReadRigVector(*Tip, TEXT("pad")) * S;
		Tips[Finger][End] = ReadRigVector(*Tip, TEXT("end")) * S;
		Tips[Finger][Core] = ReadRigVector(*Tip, TEXT("core")) * S;
	}
	Wrist = ReadRigVector(Rig, TEXT("wrist")) * S;
	Knuckle = ReadRigVector(Rig, TEXT("knuckle")) * S;
	PalmCenterY = static_cast<float>(Rig->GetNumberField(TEXT("palm_center_y")));

	// 部件网格：parts.json 里每个零件一个，掌上不动的（M1 电机、掌骨架、背盖、腕部、法兰……）不在 units 里
	for (UStaticMeshComponent* Old : MeshComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	MeshComponents.Reset();
	Parts.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *PartValues)
	{
		const TSharedPtr<FJsonObject> Part = Value->AsObject();
		const FString Name = Part->GetStringField(TEXT("name"));
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Part->GetStringField(TEXT("mesh")));
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: 灵巧手缺网格 %s"), *Name);
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner(),
			MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), FName(*(TEXT("Dex_") + Name))));
		Component->SetupAttachment(this);
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Movable);
		// 和 MannyXR 手一样是"幽灵"：不碰撞、不挡鼠标射线
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->bSelectable = false;
		Component->SetRelativeScale3D(S);
		Component->RegisterComponent();
		MeshComponents.Add(Component);

		FPart Entry;
		Entry.Mesh = Component;
		const TArray<TSharedPtr<FJsonValue>>* Level = nullptr;
		if ((*UnitsObject)->TryGetArrayField(Name, Level) && Level->Num() == 2)
		{
			const FString FingerName = (*Level)[0]->AsString();
			for (int32 Finger = 0; Finger < NumFingers; ++Finger)
			{
				if (FingerName == FingerNames[Finger])
				{
					Entry.Finger = Finger;
				}
			}
			Entry.Level = FMath::Clamp(static_cast<int32>((*Level)[1]->AsNumber()), 0, 4);
		}
		Parts.Add(Entry);
	}
	bBuilt = Parts.Num() > 0;
	UE_LOG(LogTemp, Log, TEXT("ToyBox: %s手用灵巧手模型，%d 个部件"), bLeft ? TEXT("左") : TEXT("右"), Parts.Num());
	if (bBuilt)
	{
		SetCurls(0.f, 0.f, 0.f);
	}
	return bBuilt;
}

void UToyDexHandRig::SetHandVisible(bool bNewVisible)
{
	for (UStaticMeshComponent* Component : MeshComponents)
	{
		if (Component)
		{
			Component->SetVisibility(bNewVisible);
		}
	}
}

void UToyDexHandRig::JointAngles(int32 Finger, float InIndexCurl, float InThumbCurl, float InOtherCurl, float Out[4]) const
{
	// 食指、拇指：放松 → 捏合；其余三指：放松 → 握拳
	const bool bPinchFinger = Finger == Thumb || Finger == Index;
	const float Curl = Finger == Thumb ? InThumbCurl : (Finger == Index ? InIndexCurl : InOtherCurl);
	const float* Target = bPinchFinger ? PinchAngles[Finger] : FistAngles[Finger];
	for (int32 K = 0; K < 4; ++K)
	{
		Out[K] = FMath::Lerp(RestAngles[Finger][K], Target[K], Curl);
	}
}

void UToyDexHandRig::SolveFinger(int32 Finger, const float Angles[4], FTransform Out[5]) const
{
	// 关节 k 绕过 Pivot 的 Axis 比放松姿势多转 Δ；先套本关节再套上一级（UE 里 A * B = 先 A 后 B）
	Out[0] = FTransform::Identity;
	for (int32 K = 0; K < 4; ++K)
	{
		const FJoint& Joint = Joints[Finger][K];
		const FQuat Q(Joint.Axis, FMath::DegreesToRadians(Angles[K] - RestAngles[Finger][K]));
		const FTransform Local(Q, Joint.Pivot - Q.RotateVector(Joint.Pivot));
		Out[K + 1] = Local * Out[K];
	}
}

FVector UToyDexHandRig::TipModel(int32 Finger, ETipPoint Point, const FTransform& DistalDelta) const
{
	return DistalDelta.TransformPosition(Tips[Finger][Point]);
}

void UToyDexHandRig::SetCurls(float InIndexCurl, float InThumbCurl, float InOtherCurl)
{
	if (!bBuilt)
	{
		return;
	}
	FTransform Levels[NumFingers][5];
	for (int32 Finger = 0; Finger < NumFingers; ++Finger)
	{
		float Angles[4];
		JointAngles(Finger, InIndexCurl, InThumbCurl, InOtherCurl, Angles);
		SolveFinger(Finger, Angles, Levels[Finger]);
		Distal[Finger] = Levels[Finger][4];
	}
	const FVector S(bLeft ? -1.f : 1.f, 1.f, 1.f);
	for (const FPart& Part : Parts)
	{
		if (Part.Finger == INDEX_NONE || !Part.Mesh)
		{
			continue;
		}
		// 网格顶点先镜像（左手），再按所在关节的变换摆
		const FTransform& Delta = Levels[Part.Finger][Part.Level];
		Part.Mesh->SetRelativeTransform(FTransform(Delta.GetRotation(), Delta.GetTranslation(), S));
	}
}

FVector UToyDexHandRig::GetTip(EFinger Finger, ETipPoint Point) const
{
	return GetComponentTransform().TransformPosition(TipModel(Finger, Point, Distal[Finger]));
}

FVector UToyDexHandRig::TipAtCurls(EFinger Finger, ETipPoint Point, float InIndexCurl, float InThumbCurl, float InOtherCurl) const
{
	float Angles[4];
	FTransform Levels[5];
	JointAngles(Finger, InIndexCurl, InThumbCurl, InOtherCurl, Angles);
	SolveFinger(Finger, Angles, Levels);
	return GetComponentTransform().TransformPosition(TipModel(Finger, Point, Levels[4]));
}
