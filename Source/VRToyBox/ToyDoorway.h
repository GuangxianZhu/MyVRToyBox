#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyDoorway.generated.h"

class UBoxComponent;

/**
 * 两个场景之间的门：一边一个，用同一个 DoorName 配对。由生成脚本摆在门口。
 *
 * Actor 自己的位置朝向 = 从另一边走过来时出现的地方（地面高度，面朝屋里）；Trigger 盒在它身后、贴着门。
 * 玩家的头走进 Trigger → 黑场 → 打开 TargetMap，带上 ?Arrive=<DoorName>，AToyBoxGameMode 让玩家出生在那边同名的门口。
 * 两个场景各自保留自己烘焙好的光照，所以是换关卡，不是把两套房子拼成一张图。
 */
UCLASS()
class VRTOYBOX_API AToyDoorway : public AActor
{
	GENERATED_BODY()

public:
	AToyDoorway();

	/** 两边的门用同一个名字。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FName DoorName = TEXT("Door");

	/** 走进去以后打开的关卡（/Game/... 长路径）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString TargetMap;

	/** 显示在门口的名字（日志里用）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString DisplayName;

	/** 头走进这个盒子就换场景。默认在出生点身后 40~110cm、80cm 宽：出生点本身不在盒子里，进门后不会立刻又被送回去。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> Trigger;

	/** 关卡里找 DoorName 对得上的门；没有返回 nullptr。 */
	static AToyDoorway* Find(const UWorld* World, FName Name);

	/** 不用走过去，直接穿过这扇门（调试命令 toy.Door 用）。 */
	void GoThrough();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	bool IsHeadInside() const;

	/** 开局时头要先不在盒子里，才算"走进去"（防止出生在盒子里马上又被送走）。 */
	bool bArmed = false;
	bool bLeaving = false;
	FTimerHandle LeaveTimer;
};
