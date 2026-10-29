#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ToyGrabbable.generated.h"

class AToyHand;

/** 拿着时手指弯成什么样。 */
enum class EToyGripStyle : uint8
{
	/** 全手握：方块、端盖外缘、转子轴 */
	Power,
	/** 拇指 + 食指捏：螺钉、平键这类小件 */
	Pinch,
	/** 手掌托着：整台电机这种比手大得多的东西 */
	Palm
};

/** 手在抓取那一刻提供给物体的信息。 */
struct FToyGripContext
{
	/** 手的 GripPoint 世界变换（无缩放）：X 从掌心指向手背，Z 沿手指。 */
	FTransform GripWorld;

	/** "捏"的那个点在 GripPoint 坐标系里的位置（拇指和食指指尖之间）。 */
	FVector PinchInGrip = FVector::ZeroVector;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UToyGrabbable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 能被 AToyHand 抓起来的东西：方块、电机零件、整台电机。
 *
 * 抓取流程（AToyHand::Grab）：
 *   1. ComputeHoldTransform：物体说"我应该以什么相对变换挂在手的 GripPoint 上"（抓点对齐就在这里算）；
 *   2. OnGrabbed：物体关物理、从原来的父组件上拆下来；
 *   3. 手把物体的根组件挂到 GripPoint 上，在 SnapTime 内从当前位置滑到第 1 步算出的位置。
 * 松手：手先把它拆下来，再调 OnReleased（方块恢复物理被扔出去，零件飞回原位）。
 *
 * 检测：手在 GripPoint 周围做球形重叠，查 PhysicsBody 和 WorldDynamic 两种对象类型，
 * 再看命中的 Actor 有没有实现这个接口、CanGrab() 是不是 true。
 */
class VRTOYBOX_API IToyGrabbable
{
	GENERATED_BODY()

public:
	/**
	 * 装在别的东西上、只能沿一个方向动的（柜门、抽屉，AToyArticulated）：手不把它挂到 GripPoint 上，
	 * 而是拿着的每一帧把捏合点的世界位置交给 UpdateConstrainedGrab，由它自己决定转多少 / 拉多少。
	 */
	virtual bool IsConstrained() const { return false; }
	virtual void UpdateConstrainedGrab(const FVector& HandLocation) {}

	/** 现在能不能抓（比如电机爆炸动画进行中就不能抓）。 */
	virtual bool CanGrab() const { return true; }

	/** 返回"物体根组件相对手 GripPoint"应该在的变换，缩放为 1（调用方补回物体原来的缩放）。 */
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const = 0;

	virtual void OnGrabbed(AToyHand* Hand) = 0;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) = 0;
	virtual AToyHand* GetHolder() const = 0;

	/** 手靠近时的高亮。两只手可能同时悬停，实现里用计数。 */
	virtual void AddHover() = 0;
	virtual void RemoveHover() = 0;

	virtual EToyGripStyle GetGripStyle() const { return EToyGripStyle::Power; }

	/** 拿在手里时手边浮出的信息牌。没有就返回 false。 */
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const { return false; }

	/** 材料说明（parts.json 的 material 字段那样的文字），声音和震动按它分材料（UToyFeedback::ClassifyMaterial）。空 = 硬塑料。 */
	virtual FString GetMaterialText() const { return FString(); }
};
