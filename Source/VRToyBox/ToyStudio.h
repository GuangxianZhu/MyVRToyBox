#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyStudio.generated.h"

class AToyCube;
class AToyTouchScreen;

/**
 * 6m x 6m 的小工作室：地板、四面墙、天花板、灯，以及一张桌子。
 * Actor 原点 = 玩家站立的位置（地面高度），玩家面朝 +X，桌子就在正前方。
 *
 * 房间和桌子在构造函数里建，所以把它拖进任何关卡，编辑器里立刻能看到；
 * 触屏在 BeginPlay 时生成并放到 ScreenMount 上。
 */
UCLASS()
class VRTOYBOX_API AToyStudio : public AActor
{
	GENERATED_BODY()

public:
	AToyStudio();

	/** 在桌上 SpawnPoint 附近掉下一个随机颜色的方块。数量到上限时返回 false。 */
	bool SpawnCube();
	void ClearCubes();
	int32 GetCubeCount() const;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	int32 MaxCubes = 20;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> SpawnPoint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ScreenMount;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AToyTouchScreen> Screen;

	TArray<TWeakObjectPtr<AToyCube>> Cubes;
};
