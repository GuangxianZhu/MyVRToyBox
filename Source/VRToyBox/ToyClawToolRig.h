#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ToyClawToolRig.generated.h"

class UStaticMeshComponent;

/**
 * 玩家手里的三指抓取器（参考 Red Matter 2，Content/Models/ClawTool，Tools/claw/build_claw_tool.py 建模）：
 * 一个不动的机身（握柄、圆形头、护手环）+ 三根手指，每根三节。
 *
 * 网格是"张开"姿势导入的（顶点在装配位置），让手指动起来靠 rig.json：每根手指的根关节点 base、往外张的方向 radial、
 * 关节轴 axis，每节长度，张开 / 合拢两组相对角度。第 j 节的方向 = cos(φj)·X + sin(φj)·radial，φj = 前 j+1 个相对角度之和，
 * 合拢程度 c 在两组角度之间线性插值（三根手指各自一个 c，抓住东西时各自停在碰到零件的地方）。
 *
 * 坐标：本组件的坐标系就是手 Actor 的坐标系（厘米，右手：+X 朝前、+Y 手背、+Z 拇指一侧），左手由 AToyHand 给本组件 Y 缩放 -1。
 */
UCLASS()
class VRTOYBOX_API UToyClawToolRig : public USceneComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 NumFingers = 3;
	static constexpr int32 NumSegments = 3;

	/** 读 rig.json + parts.json、生成网格组件。已经建过就直接返回 true。 */
	bool Build();
	bool IsBuilt() const { return bBuilt; }

	void SetToolVisible(bool bNewVisible);

	/** 按每根手指的合拢程度（0 = 张开，1 = 合拢）摆手指。 */
	void SetClose(const float InClose[NumFingers]);

	/** 不动网格，只算某根手指在合拢程度 Close 时指尖内侧接触小球的球心（世界坐标）。 */
	FVector TipAt(int32 Finger, float Close) const;

	float GetTipRadius() const { return TipRadius; }

	/** 三根手指全部合拢时三个指尖的中点（本组件坐标）：捏合点。 */
	FVector GetPinchLocal() const { return Pinch; }

	/**
	 * 手套怎么握它（rig.json 的 glove，build_claw_tool.py 按手套的蒙皮顶点算好的）：
	 * 中指 / 无名指 / 小指包住握柄的弯曲程度，食指搭在扳机上的弯曲程度（扣扳机时再多弯 IndexPull），拇指的基础弯曲程度，
	 * 拇指指腹要压到的点（本组件坐标，面板上方）和拇指根关节最多转多少度。
	 */
	struct FGlovePose
	{
		float FingerCurl = 1.f;
		float IndexCurl = 1.f;
		float IndexPull = 0.f;
		float ThumbCurl = 0.35f;
		float ThumbRootLimit = 30.f;
		FVector ThumbTarget = FVector::ZeroVector;
		bool bValid = false;
	};
	const FGlovePose& GetGlovePose() const { return Glove; }

private:
	struct FFinger
	{
		FVector Base = FVector::ZeroVector;
		FVector Radial = FVector::UpVector;
		FVector Axis = FVector::RightVector;
		UStaticMeshComponent* Segments[NumSegments] = {};
	};

	/** 第 j 节在合拢程度 Close 时的关节点、方向、法向（本组件坐标），Out 有 NumSegments 个；返回指尖末端点。 */
	FVector Solve(const FFinger& Finger, float Close, FVector OutPivot[NumSegments], FVector OutDir[NumSegments], FVector OutNormal[NumSegments]) const;

	/** 局部 (x, y, z) → Pivot + x·Dir + y·Normal + z·Axis。 */
	static FMatrix Frame(const FVector& Pivot, const FVector& Dir, const FVector& Normal, const FVector& Axis);

	bool bBuilt = false;
	FFinger Fingers[NumFingers];
	float Lengths[NumSegments] = {};
	float Thickness[NumSegments] = {};
	float OpenAngles[NumSegments] = {};
	float ClosedAngles[NumSegments] = {};
	float TipRadius = 0.45f;
	FVector Pinch = FVector::ZeroVector;
	FGlovePose Glove;
	/** 每节网格在张开姿势下的坐标系（求 张开 → 现在 的变换用）。 */
	FMatrix OpenFrames[NumFingers][NumSegments];

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MeshComponents;
};
