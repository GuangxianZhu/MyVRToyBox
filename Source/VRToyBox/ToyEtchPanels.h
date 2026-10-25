#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class AToyEtcherRig;
class SWidget;
struct FSlateBrush;

/** 面板用到的画刷：Slate 控件只存画刷指针，所以画刷跟着刻蚀机演示 Actor 走 */
struct FToyEtchBrushes
{
	FSlateColorBrush Flat = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush Panel = FSlateRoundedBoxBrush(FLinearColor::White, 24.f);
	FSlateRoundedBoxBrush Round = FSlateRoundedBoxBrush(FLinearColor::White, 10.f);
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 100.f);
};

/**
 * 刻蚀机演示模式的 Slate 界面：浮在空中的讲解板、剖面板、结果板、粒子图例，以及触屏刻蚀机页签里用到的平台小地图和结果列表。
 * 画法照 ITBasics 干法刻蚀机/web/app.js 的 drawProfile / drawChart / finish。全部每帧从 AToyEtcherRig 读状态，不用手动刷新。
 *
 * 颜色按"看起来的样子"写，WidgetComponent 再整体乘 BoardBrightness（和触屏一样：不受光照的 UI 在自动曝光下很暗）。
 */
namespace ToyEtchUI
{
	/** 剖面图画布（单位 nm，和网页一样 1 nm 一格）：宽 300，高 250；多晶硅顶面在 Y = 78，栅极宽 60，多晶硅厚 100，栅氧 4 nm 放大两倍画 */
	constexpr float ProfileW = 300.f;
	constexpr float ProfileH = 250.f;
	constexpr float ProfileY0 = 78.f;
	constexpr float OxScale = 2.f;

	/** 剖面图某个横坐标（nm，栅极中心为 0）上材料表面的深度：离子、自由基飞到这里就没了 */
	float ProfileSurface(const AToyEtcherRig& Rig, float X);

	/** 界面用的颜色（sRGB 十六进制，和网页一样） */
	FLinearColor Hex(uint32 Rgb, float Alpha = 1.f);

	/** 把网页文字里显示不出来的符号换掉：下标数字 ₂ → 2，上标 ⁺ → +，✓ → √（引擎默认字体的中文后备字形里没有） */
	FString VrText(const FString& Text);

	/** 讲解板：第几步 + 现在在干什么 + 为什么，下面是整个工艺的趋势曲线 */
	TSharedRef<SWidget> MakeInfoBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B);
	/** 剖面板：一根 60nm 的栅极 */
	TSharedRef<SWidget> MakeProfileBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B);
	/** 结果板（浮在右边） */
	TSharedRef<SWidget> MakeResultBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B);
	/** 放大腔下面的粒子图例 */
	TSharedRef<SWidget> MakeLegend(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B);
	/** 5 项判定的列表（结果板和触屏"结果"页共用），Scale = 字号倍数 */
	TSharedRef<SWidget> MakeResultList(TWeakObjectPtr<AToyEtcherRig> Rig, float Scale, const FSlateBrush* Round);

	/** 五种粒子的颜色：气体分子、电子、离子、自由基、产物 */
	FLinearColor ParticleColor(int32 Type);
}

/** 剖面图（网页版 drawProfile）：硅、栅氧、多晶硅、光刻胶，侧壁按每个深度的钻蚀量往里缩；离子是往下的短线，自由基是点 */
class SToyEtchProfile : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SToyEtchProfile) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AToyEtcherRig>, Rig)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TWeakObjectPtr<AToyEtcherRig> Rig;
	FSlateColorBrush White = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 100.f);
};

/** 趋势图（网页版 drawChart）：负载锁压力、腔压、源功率、反射功率、偏压、光谱六条曲线，横轴是整个工艺，终点画一条竖虚线 */
class SToyEtchTrend : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SToyEtchTrend) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AToyEtcherRig>, Rig)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TWeakObjectPtr<AToyEtcherRig> Rig;
	FSlateColorBrush White = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 100.f);
};

/** 平台俯视小地图：晶圆盒、EFEM、对准器、负载锁、传输腔、三个工艺腔，一个亮点表示晶圆现在在哪 */
class SToyEtchMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SToyEtchMap) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AToyEtcherRig>, Rig)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TWeakObjectPtr<AToyEtcherRig> Rig;
	FSlateColorBrush White = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 100.f);
};
