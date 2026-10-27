#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "ToyMenuPanel.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UTextRenderComponent;
class UToyShapeComponent;

UENUM()
enum class EToyMenuAction : uint8
{
	Start,
	OpenSettings,
	Quit,
	Turn,
	MoveSpeed,
	Teleport,
	PinchBall,
	GrabSize,
	Resolution,
	CalibrateHands,
	Sound,
	Haptics,
	Back
};

UENUM()
enum class EToyMenuPage : uint8
{
	Main,
	Settings
};

/** 菜单上的一个按钮，和触屏按钮一样：按钮面、两行字、按钮面前方看不见的触发盒。只在自己那一页显示。 */
USTRUCT()
struct FToyMenuButton
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

	EToyMenuAction Action = EToyMenuAction::Start;
	EToyMenuPage Page = EToyMenuPage::Main;
	FLinearColor Color = FLinearColor::White;
	FVector RestLocation = FVector::ZeroVector;
};

/**
 * 进游戏时飘在面前的主菜单面板：START / SETTINGS / QUIT 一页，设置一页（两列四排）。
 *
 * 交互和桌上的触屏完全一样：食指指尖戳进按钮前面的触发盒算一次点击（AToyHand::TickPoke），桌面模式鼠标点。
 * 开局由工作室生成并打开，摆在头前方 45cm、低 35cm 的地方，往后仰正对着眼睛；START 收起，
 * 之后触屏右上角的 MENU（桌面模式按 M）再叫出来，会重新摆到你现在的头前面。
 * 设置项见 ToySettings：每按一次换一档，立刻生效并存盘。
 */
UCLASS()
class VRTOYBOX_API AToyMenuPanel : public AActor
{
	GENERATED_BODY()

public:
	AToyMenuPanel();

	/** 摆到玩家头前面、显示主页。 */
	void Open();
	void Close();
	void Toggle();
	bool IsOpen() const { return bOpen; }

	/** 某个组件被戳了。是当前页的按钮且不在冷却中就执行，返回 true（手会震一下）。 */
	bool Poke(UPrimitiveComponent* Touched);

	/** 不经过戳按钮直接执行（调试命令 toy.Menu 用）。 */
	void RunAction(EToyMenuAction Action);

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UTextRenderComponent> Title;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UTextRenderComponent> Hint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TArray<FToyMenuButton> Buttons;

	/** 面板离头多远、比眼睛低多少（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float Distance = 45.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float BelowEyes = 35.f;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PressCooldown = 0.35f;

protected:
	virtual void BeginPlay() override;

private:
	UTextRenderComponent* MakeText(FName Name, const FVector& Location, const FString& Text, float Size, const FColor& Color);
	void MakeButton(const FString& Id, EToyMenuPage Page, EToyMenuAction Action, float Y, float Z, float Width, float Height,
		const FString& Line1, const FString& Line2, const FLinearColor& Color, float TextSize);
	void ShowPage(EToyMenuPage Page);
	void PlaceInFrontOfPlayer();
	void RefreshLabels();
	void Flash(int32 ButtonIndex);

	EToyMenuPage CurrentPage = EToyMenuPage::Main;
	bool bOpen = false;
	bool bStarted = false;
	double LastPressTime = -10.0;
	FTimerHandle FlashTimer;
	FTimerHandle PlaceTimer;
};
