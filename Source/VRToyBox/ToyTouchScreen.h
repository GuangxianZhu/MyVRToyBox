#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "ToyTouchScreen.generated.h"

class AToyStudio;
class UBoxComponent;
class UPrimitiveComponent;
class UTextRenderComponent;
class UToyShapeComponent;

UENUM()
enum class EToyScreenAction : uint8
{
	SpawnCube,
	ClearCubes,
	ToggleMotor,
	ToggleValve,
	ToggleDrone,
	ToggleLathe,
	ToggleExplode,
	ResetParts,
	MoveNear,
	MoveFar,
	MoveLeft,
	MoveRight
};

/** 屏幕上的一个按钮：看得见的按钮面、两行文字、按钮面前方看不见的触发盒。 */
USTRUCT()
struct FToyScreenButton
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UBoxComponent> Zone;

	UPROPERTY()
	TObjectPtr<UToyShapeComponent> Face;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Line1;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Line2;

	EToyScreenAction Action = EToyScreenAction::SpawnCube;
	FLinearColor Color = FLinearColor::White;
	FVector RestLocation = FVector::ZeroVector;
};

/**
 * 桌上的支架触屏：底座 + 立杆 + 倾斜 30° 的屏幕，屏幕上 4 排按钮：方块；四个模型；爆炸 / 复位；挪模型（左、近、远、右）。
 *
 * 食指指尖（AToyHand::PokeTip）进入某个按钮触发盒的那一帧算一次点击；桌面模式下鼠标射线打到触发盒也算。
 * 按钮只负责把动作交给 AToyStudio，状态文字和按钮上的字由 RefreshStatus 从工作室读回来。
 */
UCLASS()
class VRTOYBOX_API AToyTouchScreen : public AActor
{
	GENERATED_BODY()

public:
	AToyTouchScreen();

	void SetStudio(AToyStudio* InStudio);

	/** 某个组件被戳了。是按钮且不在冷却中就执行，返回 true（手会震一下）。 */
	bool Poke(UPrimitiveComponent* Touched);

	void RefreshStatus();

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 屏幕的转轴，屏幕朝它的 +X。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UTextRenderComponent> Status;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TArray<FToyScreenButton> Buttons;

	/** 两次点击之间的最短间隔，防止手指抖一下触发两次。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PressCooldown = 0.35f;

private:
	UTextRenderComponent* MakeText(FName Name, const FVector& Location, const FString& Text, float Size, const FColor& Color);
	/** Width = 按钮宽度（厘米），TextSize = 两行字的字高。 */
	void MakeButton(const FString& Id, EToyScreenAction Action, float Y, float Z, const FString& Line1, const FString& Line2,
		const FLinearColor& Color, float Width, float TextSize);
	FToyScreenButton* FindButton(EToyScreenAction Action);
	void Flash(int32 ButtonIndex);

	TWeakObjectPtr<AToyStudio> Studio;
	double LastPressTime = -10.0;
	FTimerHandle FlashTimer;
};
