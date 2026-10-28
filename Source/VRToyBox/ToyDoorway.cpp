#include "ToyDoorway.h"

#include "ToyBoxPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace ToyDoorwayLocal
{
	constexpr float DoorFadeSeconds = 0.3f;

	FAutoConsoleCommandWithWorldAndArgs DoorCommand(TEXT("toy.Door"),
		TEXT("直接穿过关卡里的门（不用走过去）：toy.Door [门的名字]，不写名字 = 第一扇门"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			for (TActorIterator<AToyDoorway> It(World); It; ++It)
			{
				if (Args.Num() == 0 || It->DoorName == FName(*Args[0]))
				{
					It->GoThrough();
					return;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Door 关卡里没有这扇门"));
		}));
}

AToyDoorway::AToyDoorway()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(GetRootComponent());
	Trigger->SetRelativeLocation(FVector(-75.f, 0.f, 120.f));
	Trigger->SetBoxExtent(FVector(35.f, 40.f, 120.f));
	// 只用它的形状判断头在不在里面，不参与碰撞，也不挡传送射线
	Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trigger->SetGenerateOverlapEvents(false);
}

AToyDoorway* AToyDoorway::Find(const UWorld* World, FName Name)
{
	if (World)
	{
		for (TActorIterator<AToyDoorway> It(World); It; ++It)
		{
			if (It->DoorName == Name)
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void AToyDoorway::BeginPlay()
{
	Super::BeginPlay();
	bArmed = false;
	bLeaving = false;
}

bool AToyDoorway::IsHeadInside() const
{
	const AToyBoxPawn* Pawn = Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Pawn || !Pawn->Camera)
	{
		return false;
	}
	const FVector Local = Trigger->GetComponentTransform().InverseTransformPosition(Pawn->Camera->GetComponentLocation());
	const FVector Extent = Trigger->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AToyDoorway::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLeaving)
	{
		return;
	}
	const bool bInside = IsHeadInside();
	if (!bArmed)
	{
		bArmed = !bInside;
		return;
	}
	if (bInside)
	{
		GoThrough();
	}
}

void AToyDoorway::GoThrough()
{
	if (bLeaving || TargetMap.IsEmpty())
	{
		return;
	}
	bLeaving = true;
	UE_LOG(LogTemp, Log, TEXT("ToyBox: 穿过门 %s（%s）→ %s"), *DoorName.ToString(), *DisplayName, *TargetMap);
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, ToyDoorwayLocal::DoorFadeSeconds, FLinearColor::Black, false, true);
	}
	const FString Options = FString::Printf(TEXT("Arrive=%s"), *DoorName.ToString());
	GetWorldTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this, Options]()
	{
		UGameplayStatics::OpenLevel(this, FName(*TargetMap), true, Options);
	}), ToyDoorwayLocal::DoorFadeSeconds, false);
}
