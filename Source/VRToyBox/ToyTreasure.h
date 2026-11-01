#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyTreasure.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** 六种小零件（宝物），找齐了在工作台上拼成一个小机器人。 */
enum class EToyTreasureKind : uint8
{
	Gear,
	Screw,
	Battery,
	Nut,
	Bolt,
	Bearing,
	Count
};

/**
 * 小人国寻宝：样板间里藏着 HuntSize 个亮晶晶的小零件（齿轮、螺丝、电池……，世界尺寸 1–2cm，小人眼里 15–20cm），
 * 藏在只有变小才看得见、走得到的地方：沙发底下、暖气片下面、梯架顶上、墙上搁板的花瓶后面、茶几底下的书堆上……
 *
 * 候选点（ToyTreasure.cpp 的 TreasureSpots，大约 15 个）开局用射线一个个验证（要打到朝上的真实表面、头顶放得下；
 * "底下"的点还要头顶有家具盖着），每一轮随机挑 HuntSize 个：至少一个在预设滑索落地的那一片（得先滑下去），
 * 至少一个得用 OBJECTS 的书 / 瓷砖 / 木板搭台阶才上得去。
 *
 * 小身体碰到（身体走过去、或者手捏）就收下：声音 + 震动，右手提示 FOUND 3/6；大身体捏只提示 TOO BIG。
 * 小身体走到 TreasureGlowRange（小人眼里 1.5 米）以内，宝物外面有一圈很淡的光一闪一闪，大身体时没有。
 * 找齐了在工作台上出现一个用这六个零件拼的小机器人（AToyTreasureRobot）。主菜单显示 TREASURE 3/6，按一下 = NEW HUNT 重新藏。
 * RESET PROPS 不管宝物。调试：toy.Treasure list | collect N | reset | goto N。
 */
UCLASS()
class VRTOYBOX_API AToyTreasure : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyTreasure();

	static constexpr int32 HuntSize = 6;

	/** 开一轮新的：删掉现在的宝物和机器人，从验证过的候选点里随机挑 HuntSize 个藏好。返回藏了几个。 */
	static int32 StartHunt(UWorld* World);

	/** 这一轮的宝物，按编号排好（toy.Treasure 的 N 从 1 开始）。 */
	static TArray<AToyTreasure*> GetAll(UWorld* World);

	/** 找到了几个 / 一共几个。 */
	static void GetProgress(UWorld* World, int32& OutFound, int32& OutTotal);

	/** 收下（Hand 为空 = 身体碰到的或者调试命令）。已经收过的不算。 */
	void Collect(AToyHand* Hand);

	bool IsCollected() const { return bCollected; }
	int32 GetHuntIndex() const { return HuntIndex; }
	EToyTreasureKind GetKind() const { return Kind; }
	const FString& GetSpotName() const { return SpotName; }
	/** 站在旁边看它的地方（toy.Treasure goto）：离它几厘米、头顶不撞东西的地上。 */
	bool FindViewPoint(FVector& OutFeet, float& OutYaw) const;

	static const TCHAR* KindLabel(EToyTreasureKind Kind);

	virtual void Tick(float DeltaSeconds) override;

	virtual bool IsConstrained() const override { return true; }
	virtual void UpdateConstrainedGrab(const FVector& HandLocation) override {}
	virtual bool CanGrab() const override { return !bCollected; }
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override { return FTransform::Identity; }
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override {}
	virtual AToyHand* GetHolder() const override { return nullptr; }
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override { return EToyGripStyle::Pinch; }
	virtual FString GetMaterialText() const override { return TEXT("铝"); }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 外面那圈一闪一闪的光（虚影材质的球），只在小身体走近时显示。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Glow;

	/** 手捏的检测盒（WorldDynamic、只查询，不挡小身体）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> GrabZone;

private:
	void Setup(EToyTreasureKind InKind, int32 InHuntIndex, const FString& InSpotName);
	/** 按种类用引擎基础形状拼出零件的样子（底面贴着根组件原点）。 */
	void BuildShape();
	UStaticMeshComponent* AddPart(bool bCube, bool bSphere, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color);
	/** 下一帧让手松开（手在 Grab 里调完 OnGrabbed 才把我记成"拿着的东西"）。 */
	void ReleaseHandNextTick(AToyHand* Hand);

	EToyTreasureKind Kind = EToyTreasureKind::Gear;
	int32 HuntIndex = 0;
	FString SpotName;
	bool bCollected = false;
	int32 HoverCount = 0;
	float GlowTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;
};

/** 找齐以后工作台上出现的小机器人：六种零件拼成的，慢慢转着。新的一轮开始时删掉。 */
UCLASS()
class VRTOYBOX_API AToyTreasureRobot : public AActor
{
	GENERATED_BODY()

public:
	AToyTreasureRobot();

	static AToyTreasureRobot* SpawnOnWorkbench(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

private:
	void Build();
	void AddPart(bool bCube, bool bSphere, const FVector& Location, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& Color);

	float Age = 0.f;
	float LastHop = 0.f;
};
