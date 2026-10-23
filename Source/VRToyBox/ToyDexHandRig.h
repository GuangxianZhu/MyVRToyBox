#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ToyDexHandRig.generated.h"

class UStaticMeshComponent;

/**
 * 五指灵巧手（Content/Models/DexHand，ITBasics 五指灵巧手）当玩家的手用：50 个静态网格部件 + 每根手指 J1→J4 一条关节链。
 *
 * 网格是爆炸图导入的那一套（顶点在装配位置、原点在法兰中心、手是"放松"手势），不用重新导入；
 * 让手指动起来靠 rig.json（ITBasics export_ue.py 生成）：每个关节在放松姿势下的转轴（过哪个点、朝哪个方向），
 * 加上 rest / open / fist / pinch 几个手势的关节角。关节 k 比放松姿势多转 Δ，部件跟着它所在的最深一级关节走。
 *
 * 弯曲程度和 MannyXR 手的约定对齐（AToyHand 的 IndexCurl / ThumbCurl / FingerCurl）：
 *   食指、拇指：0 = 放松，1 = pinch 手势（两个指腹刚好碰上，扳机按满）；
 *   中指、无名指、小指：0 = 放松，1 = fist 握拳（握把按满）。
 * 关节角按弯曲程度在两个手势之间线性插值（比骨骼姿势的四元数插值更像真的电机在转）。
 *
 * 坐标：本组件的坐标系就是"模型坐标"（厘米，未缩放；右手 +Z 指尖、手心 +Y、拇指 +X）。
 * 左手把模型沿 X 镜像（拇指到 -X），网格用 X 缩放 -1，关节轴换成 -(镜像后的轴) 保持转动方向。
 * 本组件在手 Actor 里怎么摆（让手指沿 +X、掌心中心在 (0, ±PalmY, 0)）由 AToyHand::SetupHandMesh 算。
 */
UCLASS()
class VRTOYBOX_API UToyDexHandRig : public USceneComponent
{
	GENERATED_BODY()

public:
	enum EFinger : uint8 { Thumb, Index, Middle, Ring, Little, NumFingers };

	/** 指尖上的哪个点：Pad = 指腹表面中心（捏的接触点），End = 指尖最前端（戳触屏），
	 *  Core = 硅胶指尖套的中心（离指腹约 0.7cm，指尖贴表面扫描时当小球的球心）。 */
	enum ETipPoint : uint8 { Pad, End, Core, NumTipPoints };

	/** 读 rig.json + parts.json、生成部件网格。已经建过且左右手相同就直接返回 true。 */
	bool Build(bool bInLeft);
	bool IsBuilt() const { return bBuilt; }

	/** 显示 / 隐藏所有部件网格。 */
	void SetHandVisible(bool bNewVisible);

	/** 按弯曲程度摆手指（见类注释），会更新部件网格的位置。 */
	void SetCurls(float InIndexCurl, float InThumbCurl, float InOtherCurl);

	/** 手指的指尖（世界坐标），按当前摆好的手势。 */
	FVector GetTip(EFinger Finger, ETipPoint Point) const;

	/** 不动网格，只算某组弯曲程度下的指尖位置（世界坐标）。捏合点锚定、指尖贴表面扫描用。 */
	FVector TipAtCurls(EFinger Finger, ETipPoint Point, float InIndexCurl, float InThumbCurl, float InOtherCurl) const;

	/** 模型坐标里（左手已镜像）的手腕点、中指根关节、掌心厚度中线的 Y：AToyHand 用来把手摆进握把姿态。 */
	FVector GetWristModel() const { return Wrist; }
	FVector GetKnuckleModel() const { return Knuckle; }
	float GetPalmCenterY() const { return PalmCenterY; }

	/** 手心朝向（模型坐标，左右手都是 +Y）。 */
	FVector GetPalmDirModel() const { return FVector(0.f, 1.f, 0.f); }

private:
	struct FJoint
	{
		FVector Pivot = FVector::ZeroVector;
		FVector Axis = FVector::XAxisVector;
	};

	struct FPart
	{
		UStaticMeshComponent* Mesh = nullptr;
		int32 Finger = INDEX_NONE;   // INDEX_NONE = 掌上不动的部件
		int32 Level = 0;              // 1~4 = 跟着 J1~J4 走
	};

	/** 一根手指在某组弯曲程度下的关节角（度）。 */
	void JointAngles(int32 Finger, float InIndexCurl, float InThumbCurl, float InOtherCurl, float Out[4]) const;

	/** 一根手指的正向运动学：Out[k] = 第 k 级关节（0 = 掌）相对放松姿势的变换（模型坐标）。 */
	void SolveFinger(int32 Finger, const float Angles[4], FTransform Out[5]) const;

	FVector TipModel(int32 Finger, ETipPoint Point, const FTransform& Distal) const;

	bool bBuilt = false;
	bool bLeft = false;

	FJoint Joints[NumFingers][4];
	float RestAngles[NumFingers][4] = {};
	float PinchAngles[NumFingers][4] = {};
	float FistAngles[NumFingers][4] = {};
	FVector Tips[NumFingers][NumTipPoints];
	FVector Wrist = FVector::ZeroVector;
	FVector Knuckle = FVector::ZeroVector;
	float PalmCenterY = 0.f;

	TArray<FPart> Parts;

	/** 当前摆好的每根手指的远节（J4）变换，GetTip 用。 */
	FTransform Distal[NumFingers];

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MeshComponents;
};
