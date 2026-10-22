#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyTouchScreen.generated.h"

class AToyStudio;
class SWidget;
class UBoxComponent;
class UWidgetComponent;
struct FSlateColor;
struct FToyScreenBrushes;

UENUM()
enum class EToyScreenAction : uint8
{
	SpawnCube,
	ClearCubes,
	ToggleMotor,
	ToggleValve,
	ToggleDrone,
	ToggleLathe,
	ToggleMultimeter,
	ToggleServer,
	ToggleOrb,
	ToggleExplode,
	ResetParts,
	MoveNear,
	MoveFar,
	MoveLeft,
	MoveRight,
	OpenMenu,
	HideModel,
	PrevPage,
	NextPage
};

/** 界面上一块能点的区域（按钮或模型卡片），坐标是界面像素（左上角为原点）。 */
struct FToyScreenItem
{
	EToyScreenAction Action = EToyScreenAction::SpawnCube;
	FBox2D Rect = FBox2D(ForceInit);
	FLinearColor Color = FLinearColor::White;
	/** 模型卡片在第几页；INDEX_NONE = 每页都有。 */
	int32 Page = INDEX_NONE;
	/** 模型卡片对应的模型序号（ModelTable 里的下标），不是卡片就是 INDEX_NONE。 */
	int32 Model = INDEX_NONE;
};

/** 一根正在屏幕前的手指（或桌面模式的鼠标）。 */
struct FToyScreenFinger
{
	const UObject* Owner = nullptr;
	FVector2D Pixel = FVector2D::ZeroVector;
	/** 指尖离屏幕表面多远（厘米，屏幕前方为正）。 */
	float Distance = 100.f;
	float LastDistance = 100.f;
	int32 Hover = INDEX_NONE;
	bool bActive = false;
	bool bPressed = false;
	bool bMouse = false;
};

/**
 * 桌上的支架触屏：底座 + 立杆 + 倾斜 30° 的屏幕。屏幕上是一块真正的 GUI（WidgetComponent 里放 C++ 写的 Slate 界面，能显示中文）：
 * 顶栏（标题、提示、菜单），左边 4×2 的模型卡片（七个模型 + 收起），下面生成 / 清空方块，右边当前模型的爆炸、复位和挪动方向键。
 *
 * 界面按钮只是贴图上的像素，没有碰撞体，所以手指不是"碰到按钮"，而是被当成一只鼠标：
 * 指尖投影到屏幕平面上换算成界面像素，再看指尖离屏幕多远——3cm 以内悬停（按钮变亮、出现光标点），
 * 从前面戳到 0.6cm 以内算按下（执行按钮、手柄震一下），退回 1.5cm 以外才算松开，松开前不会再按第二次。
 * 桌面模式下鼠标射线打到屏幕的点同样换算成像素，左键点击。
 *
 * 按钮只负责把动作交给 AToyStudio；界面上的字和颜色每帧从工作室读回来，不用手动刷新。
 */
UCLASS()
class VRTOYBOX_API AToyTouchScreen : public AActor
{
	GENERATED_BODY()

public:
	AToyTouchScreen();

	void SetStudio(AToyStudio* InStudio);

	/** 手指在屏幕前的感应区（TouchZone）里时每帧调用。这一帧按下了某个按钮就返回 true（手会震一下）。 */
	bool TouchFinger(const UObject* Finger, const FVector& TipWorld);

	/** 手指离开了感应区。 */
	void ReleaseFinger(const UObject* Finger);

	/** 桌面模式：鼠标射线打在屏幕上的点。bClick = 这一帧按下了左键；点中按钮返回 true。 */
	bool MouseAt(const FVector& WorldPoint, bool bClick);

	/** 桌面模式：鼠标移出了屏幕。 */
	void MouseLeave();

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 屏幕的转轴，屏幕朝它的 +X。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Pivot;

	/** 显示界面的那块面板，贴在显示屏表面。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UWidgetComponent> Gui;

	/** 屏幕前方看不见的感应区：手指尖进来了才开始算悬停 / 按下。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> TouchZone;

	/** 贴着屏幕表面的薄片，只挡 Visibility 射线，给桌面模式的鼠标点。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> ClickPlane;

	/** 指尖离屏幕多近开始悬停（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float HoverCm = 3.f;

	/** 指尖从前面戳到离屏幕多近算按下（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PressCm = 0.6f;

	/** 按下后指尖退回多远才算松开（厘米）。比 PressCm 大一截，手指在临界处抖动不会连点。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ReleaseCm = 1.5f;

	/** 两次点击之间的最短间隔（秒），两只手一起戳也只算一次。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float PressCooldown = 0.3f;

	/** 界面整体提亮倍数：不受光照的 UI 在自动曝光下很暗（和信息牌同一个原因）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float Brightness = 8.f;

protected:
	virtual void BeginPlay() override;

private:
	void BuildItems();
	TSharedRef<SWidget> BuildGui();
	TSharedRef<SWidget> BuildItemContent(int32 Index);

	int32 HitTest(const FVector2D& Pixel) const;
	bool IsItemVisible(int32 Index) const;
	bool IsItemEnabled(int32 Index) const;
	bool IsItemHovered(int32 Index) const;
	bool IsItemSelected(int32 Index) const;
	bool IsItemFlashing(int32 Index) const;
	FSlateColor ItemColor(int32 Index) const;
	int32 PageCount() const;

	/** 执行一个按钮。冷却中或按钮不可用时返回 false。 */
	bool Press(int32 Index);

	FToyScreenFinger* FindFinger(const UObject* Owner, bool bCreate);
	void UpdateFinger(FToyScreenFinger& Finger, const FVector& WorldPoint);

	FText StatusText() const;
	FText ModelTitle() const;
	FText ModelState() const;
	FText CubeText() const;
	FText DistanceText() const;
	FText ItemLabel(int32 Index) const;

	TWeakObjectPtr<AToyStudio> Studio;
	TArray<FToyScreenItem> Items;
	/** 最多三根"手指"：左手、右手、鼠标。 */
	FToyScreenFinger Fingers[3];
	int32 Page = 0;
	int32 FlashIndex = INDEX_NONE;
	double FlashUntil = 0.0;
	double LastPressTime = -10.0;
	TSharedPtr<FToyScreenBrushes> Brushes;
};
