#pragma once

#include "CoreMinimal.h"
#include "ToyProp.h"
#include "ToySpawnedProp.generated.h"

/** 菜单 OBJECTS 页能摆出来的东西。 */
UENUM()
enum class EToySpawnItem : uint8
{
	Book,
	Block,
	Plank,
	Box,
	Cup,
	Tile,
	Coaster
};

/**
 * 菜单 OBJECTS 页（或 toy.Spawn）摆出来的东西：书、积木、木板、盒子、杯子、瓷砖、杯垫。给小人国搭台阶、斜坡、平台用。
 *
 * 就是一个 AToyProp：捏取、物理、声音、高亮都一样，碰撞挡 Pawn（小身体能站上去）。
 * 刚摆出来时悬在面前（不受重力、一直亮着），第一次捏起来以后才变成普通的物理摆件。
 * 样子优先用本机素材包里合适的网格（ArchViz 的书和盒子、Classic Office 的杯子，都不进仓库），没有就用引擎的基础形状 + 颜色。
 * 最多 MaxSpawned 个，再摆就把最早的那个拿掉；菜单 CLEAR ALL 全部删掉；RESET PROPS 不管它们。
 */
UCLASS()
class VRTOYBOX_API AToySpawnedProp : public AToyProp
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxSpawned = 40;

	/** 在 Transform 处生成一件。bFloating = 悬着等人来拿（菜单）；否则是睡着的物理摆件（toy.Spawn 摆台阶测试用）。 */
	static AToySpawnedProp* Spawn(UWorld* World, EToySpawnItem Item, const FTransform& Transform, bool bFloating);

	/** 菜单用：悬在头前方 30cm、偏右、胸口高度，已经有悬着的就往旁边 / 上面错开。 */
	static AToySpawnedProp* SpawnInFrontOfPlayer(UWorld* World, EToySpawnItem Item);

	/** 现在一共摆了几个。 */
	static int32 CountAll(UWorld* World);

	/** 全部删掉，返回删了几个。 */
	static int32 ClearAll(UWorld* World);

	/** 现在悬着、还没被拿过的有几个（菜单摆新东西时往旁边错开用）。 */
	static int32 CountFloating(UWorld* World);

	static bool ParseItem(const FString& Name, EToySpawnItem& OutItem);
	static FString ItemLabel(EToySpawnItem Item);

	/** 摆上一件时是不是因为满了拿掉了最早的那个（菜单提示用）。 */
	static bool bLastSpawnRemovedOldest;

	virtual void OnGrabbed(AToyHand* Hand) override;

	bool IsFloating() const { return bFloating; }

private:
	/** 网格、材质、信息牌；返回这一种自己的摆法（素材网格摆平的转动、基础形状的缩放）。 */
	FTransform Setup(EToySpawnItem InItem);

	EToySpawnItem Item = EToySpawnItem::Block;
	bool bFloating = false;
	int64 Serial = 0;
};
