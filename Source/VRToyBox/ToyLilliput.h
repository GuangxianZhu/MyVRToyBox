#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyProp.h"
#include "ToyLilliput.generated.h"

class UToyShapeComponent;
class USkeletalMeshComponent;

/**
 * 小人国模式（design/小人国模式方案.md）：玩家有两具身体，大的（正常身高）和小的（1/12，眼睛离地约 13cm）。
 *
 * 变小不是缩放场景，而是改 WorldSettings->WorldToMeters（100 → 100 × 比例）：头显的追踪位移和双眼间距都按它换算，
 * 世界看起来就变巨大了。手、菜单、胶囊体、走路速度、传送抛物线这些按厘米写的东西由各自的代码乘 GetScale()。
 *
 * 默认开（toy.Lilliput 1）：样板间里不再摆桌子、触屏和爆炸图模型，开局在脚边放一个小人偶（AToyFigurine）。
 * 大的时候可以把小人偶捏起来放到任何地方，按左手柄 Y（桌面模式 G）就"变成"它：小身体出现在小人偶站的位置，
 * 大身体留在原地变成一尊不动的大人像（AToyGiantBody）。再按一次变回大身体，小人偶留在小身体最后站的地方。
 * toy.Lilliput 0 = 回到原来带桌子和爆炸图模型的玩法（下次开局生效）。
 */
namespace ToyLilliput
{
	/** 小身体的比例。 */
	constexpr float TinyScale = 1.f / 12.f;

	/** 小人国模式开没开（toy.Lilliput）。 */
	bool IsEnabled();

	/** 玩家现在的比例：1 = 大身体，TinyScale = 小身体。手、菜单、Pawn 按厘米写的距离都乘它。 */
	float GetScale();

	/** 设比例：改 WorldToMeters 和近裁剪面（默认 10cm，小身体时眼前 10cm 在小人眼里是 1.2 米，会把近处的东西切掉）。 */
	void SetScale(UWorld* World, float Scale);
}

/**
 * 小人偶：大身体时摆在场景里的"小身体"。和其他摆件一样能捏起来、松手有物理；按 Y 时小身体出现在它脚下。
 * 看得见的是一个 14cm 高的人体模特（引擎模板的 SKM_Manny，launch.bat 从引擎拷到本机 Content/Characters，不进仓库）；
 * 物理和碰撞还是那根 5cm 粗、10cm 高的圆柱（藏起来）。没有模特就显示圆柱 + 4cm 的球头，总高约 14cm（小身体眼睛离地 13cm）。
 */
UCLASS()
class VRTOYBOX_API AToyFigurine : public AToyProp
{
	GENERATED_BODY()

public:
	AToyFigurine();

	/** 关卡里的小人偶（没有就是空）。 */
	static AToyFigurine* Find(const UWorld* World);

	/** 藏起来（玩家变成了它）或者在 FeetLocation 站好、朝 Yaw 显示出来。 */
	void Hide();
	void StandAt(const FVector& FeetLocation, float Yaw);

	/** 脚下那一点：从身体中心往下找最近的表面，找不到就用包围盒底。 */
	FVector GetFeetLocation() const;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> Head;

	/** 人体模特（只看，不碰撞）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USkeletalMeshComponent> Figure;

	virtual void AddHover() override;
	virtual void RemoveHover() override;

protected:
	virtual void BeginPlay() override;
};

/**
 * 小身体时留在原地的大身体：不会动的人像，和小人偶同一个人体模特放大到玩家的身高，挡路（碰撞是藏起来的圆柱），小人可以绕着它看。
 * 没有模特时显示圆柱身体 + 球头 + 指着朝向的鼻子。
 */
UCLASS()
class VRTOYBOX_API AToyGiantBody : public AActor
{
	GENERATED_BODY()

public:
	AToyGiantBody();

	/** 脚站在 Feet、面朝 Yaw，头顶的眼睛在 EyeHeight（厘米，离脚）的位置。 */
	void ShowAt(const FVector& Feet, float Yaw, float EyeHeight);
	void Hide();

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> Head;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> Nose;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USkeletalMeshComponent> Figure;
};
