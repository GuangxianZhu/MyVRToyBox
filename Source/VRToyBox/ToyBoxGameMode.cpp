#include "ToyBoxGameMode.h"

#include "ToyBoxPawn.h"
#include "ToyStudio.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AToyBoxGameMode::AToyBoxGameMode()
{
	DefaultPawnClass = AToyBoxPawn::StaticClass();
}

void AToyBoxGameMode::StartPlay()
{
	GetOrSpawnStudio();
	Super::StartPlay();
}

AActor* AToyBoxGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// 玩家总是站在工作室原点，不用关卡里的 PlayerStart：
	// 引擎自带的 Entry 地图里本来就有一个 PlayerStart，用它的话人会出生在工作室外面，眼前一片黑
	AToyStudio* Studio = GetOrSpawnStudio();
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 玩家出生在工作室 %s 的原点 %s"), *GetNameSafe(Studio),
		Studio ? *Studio->GetActorLocation().ToString() : TEXT("-"));
	return Studio;
}

AToyStudio* AToyBoxGameMode::GetOrSpawnStudio()
{
	for (TActorIterator<AToyStudio> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<AToyStudio>(AToyStudio::StaticClass(), FTransform::Identity, Params);
}
