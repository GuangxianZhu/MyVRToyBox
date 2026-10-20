#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ToyBoxGameMode.generated.h"

class AToyStudio;

/**
 * 关卡里没有 AToyStudio 就自动生成一个，并让玩家站在它的原点。
 * 所以用一张完全空的地图（默认是引擎自带的 /Engine/Maps/Entry）也能直接玩。
 */
UCLASS()
class VRTOYBOX_API AToyBoxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AToyBoxGameMode();

	virtual void StartPlay() override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

private:
	AToyStudio* GetOrSpawnStudio();
};
