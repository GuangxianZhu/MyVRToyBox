#include "ToyBoxGameMode.h"

#include "ToyBoxPawn.h"
#include "ToyStudio.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

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
	// 关卡里自己放了 PlayerStart 就用它；找不到时引擎会退回 WorldSettings（即原点），这时改用工作室原点
	AActor* Start = Super::FindPlayerStart_Implementation(Player, IncomingName);
	if (Start && !Start->IsA<AWorldSettings>())
	{
		return Start;
	}
	return GetOrSpawnStudio();
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
