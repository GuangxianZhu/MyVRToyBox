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

/**
 * 桌上的支架触屏：底座 + 立杆 + 倾斜 30° 的屏幕，屏幕上两个按钮。
 *
 * 每个按钮由三部分组成：看得见的按钮面（Face）、文字、以及按钮面前方一个看不见的触发盒（Zone）。
 * 食指指尖（AToyHand::PokeTip）进入触发盒的那一帧算一次点击；桌面模式下鼠标射线打到触发盒也算。
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
	TObjectPtr<UBoxComponent> SpawnZone;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> ClearZone;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> SpawnFace;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UToyShapeComponent> ClearFace;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UTextRenderComponent> Status;

	/** 两次点击之间的最短间隔，防止手指抖一下触发两次。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PressCooldown = 0.35f;

private:
	UTextRenderComponent* MakeText(FName Name, const FVector& Location, const FString& Text, float Size, const FColor& Color);
	UBoxComponent* MakeButton(const FString& Id, float Y, const FString& Line1, const FString& Line2,
		const FLinearColor& Color, TObjectPtr<UToyShapeComponent>& OutFace);
	void Flash(UToyShapeComponent* Face, const FLinearColor& Color);

	TWeakObjectPtr<AToyStudio> Studio;
	double LastPressTime = -10.0;
	FTimerHandle FlashTimer;
};
