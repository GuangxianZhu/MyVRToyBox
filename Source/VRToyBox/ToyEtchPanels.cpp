#include "ToyEtchPanels.h"

#include "ToyEtcherRig.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>

namespace
{
	// 和触屏同一套颜色（按"看起来的样子"写，WidgetComponent 再整体提亮）
	const FLinearColor BackColor(0.02f, 0.03f, 0.06f);
	const FLinearColor PanelColor(0.045f, 0.06f, 0.1f);
	const FLinearColor TextColor(1.f, 1.f, 1.f);
	const FLinearColor DimTextColor(0.55f, 0.65f, 0.8f);
	const FLinearColor TitleColor(1.f, 0.8f, 0.3f);
	const FLinearColor ErrorColor(1.f, 0.45f, 0.35f);
	const FLinearColor OkColor(0.35f, 0.9f, 0.45f);
	const FLinearColor ChartBack(0.035f, 0.045f, 0.08f);

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		// 引擎默认字体带中文后备字形（触屏、信息牌也是用它）
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** 画矩形、线、字的小工具：坐标都是控件本地像素 */
	struct FPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Out;
		int32 Layer;
		const FSlateBrush* White;

		void Box(float X, float Y, float W, float H, const FLinearColor& Color, int32 Extra = 0) const
		{
			if (W <= 0.f || H <= 0.f)
			{
				return;
			}
			FSlateDrawElement::MakeBox(Out, Layer + Extra, Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
				White, ESlateDrawEffect::None, Color);
		}

		void Lines(TArray<FVector2f> Points, const FLinearColor& Color, float Thickness, int32 Extra = 1) const
		{
			if (Points.Num() < 2)
			{
				return;
			}
			FSlateDrawElement::MakeLines(Out, Layer + Extra, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Thickness);
		}

		void Text(float X, float Y, const FString& String, const FSlateFontInfo& FontInfo, const FLinearColor& Color, int32 Extra = 2) const
		{
			FSlateDrawElement::MakeText(Out, Layer + Extra, Geometry.ToPaintGeometry(FVector2f(1000.f, 60.f), FSlateLayoutTransform(FVector2f(X, Y))),
				String, FontInfo, ESlateDrawEffect::None, Color);
		}
	};

	/** 趋势图的一条曲线：取 FSample 的哪一项、对数还是线性、上限、颜色（网页版 SERIES） */
	struct FSeriesSpec
	{
		const TCHAR* Key;
		const TCHAR* Name;
		const TCHAR* Unit;
		bool bLog;
		double Min;
		double Max;
		uint32 Color;
	};

	const FSeriesSpec SeriesSpecs[] = {
		{ TEXT("llP"), TEXT("负载锁"), TEXT("Torr"), true, 0.05, 1000, 0x8a7dd8 },
		{ TEXT("chP"), TEXT("腔压"), TEXT("mTorr"), false, 0, 90, 0x3a8fd6 },
		{ TEXT("srcF"), TEXT("源功率"), TEXT("W"), false, 0, 1600, 0xe0761c },
		{ TEXT("srcR"), TEXT("反射功率"), TEXT("W"), false, 0, 400, 0xd64545 },
		{ TEXT("biasF"), TEXT("偏压"), TEXT("W"), false, 0, 420, 0x14968f },
		{ TEXT("oes"), TEXT("光谱（含硅产物）"), TEXT(""), false, 0, 1.05, 0xb8a400 },
	};

	double SampleValue(const ToyEtch::FSample& Sample, int32 Series)
	{
		switch (Series)
		{
		case 0: return Sample.LlP;
		case 1: return Sample.ChP;
		case 2: return Sample.SrcF;
		case 3: return Sample.SrcR;
		case 4: return Sample.BiasF;
		default: return Sample.Oes;
		}
	}

	/** 网页版 fmt()：负载锁压力跨好几个数量级，小数位跟着变 */
	FString FormatValue(double Value, int32 Series)
	{
		switch (Series)
		{
		// Printf 的格式串必须是编译期常量，不能用三目选格式
		case 0:
			if (Value >= 10) { return FString::Printf(TEXT("%.0f"), Value); }
			if (Value >= 1) { return FString::Printf(TEXT("%.1f"), Value); }
			return FString::Printf(TEXT("%.2f"), Value);
		case 1:
			if (Value < 1) { return FString::Printf(TEXT("%.2f"), Value); }
			return FString::Printf(TEXT("%.1f"), Value);
		case 5: return FString::Printf(TEXT("%.2f"), Value);
		default: return FString::Printf(TEXT("%.0f"), Value);
		}
	}

	TSharedRef<SWidget> Swatch(const FSlateBrush* Brush, const FLinearColor& Color, float Size)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(Color)
		];
	}

	/** 图例的一项：色块 + 名字 */
	TSharedRef<SWidget> LegendItem(const FSlateBrush* Brush, const FLinearColor& Color, const FString& Name, int32 FontSize)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				Swatch(Brush, Color, FontSize * 0.8f)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Name)).Font(Font(FontSize)).ColorAndOpacity(TextColor)
			];
	}
}

FLinearColor ToyEtchUI::Hex(uint32 Rgb, float Alpha)
{
	FLinearColor Color = FLinearColor::FromSRGBColor(FColor((Rgb >> 16) & 0xff, (Rgb >> 8) & 0xff, Rgb & 0xff));
	Color.A = Alpha;
	return Color;
}

FLinearColor ToyEtchUI::ParticleColor(int32 Type)
{
	// 网页版 PT：气体分子、电子、离子、自由基、产物
	static const uint32 Colors[] = { 0xdfe6ee, 0x6ff3ff, 0xff3d63, 0xc9f23a, 0xff9a2e };
	return Hex(Colors[FMath::Clamp(Type, 0, 4)]);
}

FString ToyEtchUI::VrText(const FString& Text)
{
	FString Out = Text;
	static const TCHAR* const Pairs[][2] = {
		{ TEXT("₀"), TEXT("0") }, { TEXT("₁"), TEXT("1") }, { TEXT("₂"), TEXT("2") }, { TEXT("₃"), TEXT("3") }, { TEXT("₄"), TEXT("4") },
		{ TEXT("₅"), TEXT("5") }, { TEXT("₆"), TEXT("6") }, { TEXT("⁺"), TEXT("+") }, { TEXT("⁻"), TEXT("-") }, { TEXT("✓"), TEXT("√") },
	};
	for (const auto& Pair : Pairs)
	{
		Out.ReplaceInline(Pair[0], Pair[1], ESearchCase::CaseSensitive);
	}
	return Out;
}

float ToyEtchUI::ProfileSurface(const AToyEtcherRig& Rig, float X)
{
	// 网页版 surf()：栅极顶上是光刻胶（光刻胶没了就是被削掉的栅极顶），两边是还没刻完的多晶硅，刻穿后是栅氧（刻穿栅氧就是硅）
	const ToyEtch::FEtch& E = Rig.GetProcess().E;
	const float OxTop = ProfileY0 + 100.f;
	const float SiTop = OxTop + 4.f * OxScale;
	if (FMath::Abs(X) < 30.f - E.SideCut[0])
	{
		return E.MaskH > 0 ? ProfileY0 - static_cast<float>(E.MaskH) : ProfileY0 + static_cast<float>(E.GateLoss);
	}
	if (E.Dc < 100)
	{
		return ProfileY0 + static_cast<float>(E.Dc);
	}
	return SiTop - static_cast<float>(E.Ox) * OxScale + static_cast<float>(E.Si);
}

// ---------- 剖面图 ----------

void SToyEtchProfile::Construct(const FArguments& InArgs)
{
	Rig = InArgs._Rig;
}

FVector2D SToyEtchProfile::ComputeDesiredSize(float) const
{
	return FVector2D(ToyEtchUI::ProfileW * 2.f, ToyEtchUI::ProfileH * 2.f);
}

int32 SToyEtchProfile::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AToyEtcherRig* RigActor = Rig.Get();
	if (!RigActor)
	{
		return LayerId;
	}
	using namespace ToyEtchUI;
	const ToyEtch::FProcess& P = RigActor->GetProcess();
	const ToyEtch::FEtch& E = P.E;

	// 网页里 1 nm = 1 像素，这里整体放大到铺满控件、居中
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Scale = FMath::Min(static_cast<float>(Size.X) / ProfileW, static_cast<float>(Size.Y) / ProfileH);
	const FVector2f Origin((static_cast<float>(Size.X) - ProfileW * Scale) * 0.5f, (static_cast<float>(Size.Y) - ProfileH * Scale) * 0.5f);
	const FPainter Paint{ AllottedGeometry, OutDrawElements, LayerId, &White };
	auto Box = [&Paint, Scale, Origin](float X, float Y, float W, float H, const FLinearColor& Color, int32 Extra = 0)
	{
		Paint.Box(Origin.X + X * Scale, Origin.Y + Y * Scale, W * Scale, H * Scale, Color, Extra);
	};
	auto At = [Scale, Origin](float X, float Y) { return FVector2f(Origin.X + X * Scale, Origin.Y + Y * Scale); };

	const float W = ProfileW, Hh = ProfileH;
	auto X = [W](float Nm) { return W * 0.5f + Nm; };
	auto Yp = [](float Depth) { return ProfileY0 + Depth; };
	const float OxTop = Yp(100.f);
	const float SiTop = OxTop + 4.f * OxScale;
	const FLinearColor Poly = Hex(0xc6a86c);
	const FLinearColor Oxide = Hex(0xc9d6f0);
	const FLinearColor Back = Hex(0x1a2030);

	Box(0.f, 0.f, W, Hh, Back);
	// 硅（刻穿栅氧以后被挖下去的部分画成空的，红框）
	Box(0.f, SiTop, W, Hh - SiTop, Hex(0x6b7487));
	const float Si = static_cast<float>(E.Si);
	if (Si > 0.f)
	{
		Box(0.f, SiTop, X(-30.f), Si, Back);
		Box(X(30.f), SiTop, W - X(30.f), Si, Back);
		const FLinearColor Red = Hex(0xff6b6b);
		Paint.Lines({ At(1.f, SiTop), At(1.f, SiTop + Si), At(X(-30.f), SiTop + Si), At(X(-30.f), SiTop) }, Red, 1.5f * Scale);
		Paint.Lines({ At(X(30.f), SiTop), At(X(30.f), SiTop + Si), At(W - 1.f, SiTop + Si), At(W - 1.f, SiTop) }, Red, 1.5f * Scale);
	}
	// 栅氧：栅极下面那段一直在；两边按剩下的厚度画（4 nm 放大两倍）
	Box(X(-30.f), OxTop, 60.f, 4.f * OxScale, Oxide);
	const float OxNow = static_cast<float>(E.Ox) * OxScale;
	Box(0.f, SiTop - OxNow, X(-30.f), OxNow, Oxide);
	Box(X(30.f), SiTop - OxNow, W - X(30.f), OxNow, Oxide);
	// 两边还没刻完的多晶硅
	const float Dc = static_cast<float>(E.Dc);
	if (Dc < 100.f)
	{
		Box(0.f, Yp(Dc), X(-30.f) + 0.5f, 100.f - Dc, Poly);
		Box(X(30.f) - 0.5f, Yp(Dc), W - X(30.f), 100.f - Dc, Poly);
	}
	// 栅极：每 1 nm 深度一条，左右按这个深度的侧向刻蚀量往里缩
	const int32 I0 = FMath::FloorToInt(static_cast<float>(E.GateLoss));
	if (I0 < 100)
	{
		for (int32 I = I0; I < 100; ++I)
		{
			const float Left = X(-30.f + E.SideCut[I]);
			const float Right = X(30.f - E.SideCut[I]);
			Box(Left, Yp(static_cast<float>(I)), Right - Left, 1.05f, Poly);
		}
		// 刻蚀时侧壁上的钝化层（HBr / O₂ 带来的沉积物），越多越亮
		const float Pass = static_cast<float>(P.M.pass);
		if (P.S.bEtching && Pass > 0.2f)
		{
			const int32 Reach = FMath::Min(100, FMath::FloorToInt(Dc));
			TArray<FVector2f> LeftWall, RightWall;
			for (int32 I = I0; I <= Reach; ++I)
			{
				LeftWall.Add(At(X(-30.f + E.SideCut[I]) - 0.8f, Yp(static_cast<float>(I))));
				RightWall.Add(At(X(30.f - E.SideCut[I]) + 0.8f, Yp(static_cast<float>(I))));
			}
			const FLinearColor Glow = Hex(0xffaa5a, 0.4f + Pass * 0.6f);
			Paint.Lines(MoveTemp(LeftWall), Glow, (1.f + Pass * 1.5f) * Scale);
			Paint.Lines(MoveTemp(RightWall), Glow, (1.f + Pass * 1.5f) * Scale);
		}
	}
	// 光刻胶
	const float MaskH = static_cast<float>(E.MaskH);
	if (MaskH > 0.f)
	{
		Box(X(-30.f), Yp(-MaskH), 60.f, MaskH, Hex(0xc4455a));
	}
	// 离子（往下的短线）和自由基（点）
	const FLinearColor IonColor = Hex(0x7fd8ff);
	const FLinearColor RadColor = Hex(0xffb347);
	for (const FToyProfileDot& Pt : RigActor->GetProfileDots())
	{
		if (Pt.bIon)
		{
			Paint.Lines({ At(X(Pt.X), Pt.Y), At(X(Pt.X - Pt.VX * 0.02f), Pt.Y - Pt.VY * 0.02f) }, IonColor, 1.2f * Scale);
		}
		else
		{
			Box(X(Pt.X) - 1.f, Pt.Y - 1.f, 2.2f, 2.2f, RadColor, 1);
		}
	}
	return LayerId + 3;
}

// ---------- 趋势图 ----------

void SToyEtchTrend::Construct(const FArguments& InArgs)
{
	Rig = InArgs._Rig;
}

FVector2D SToyEtchTrend::ComputeDesiredSize(float) const
{
	return FVector2D(1000.f, 330.f);
}

int32 SToyEtchTrend::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AToyEtcherRig* RigActor = Rig.Get();
	if (!RigActor)
	{
		return LayerId;
	}
	const ToyEtch::FProcess& P = RigActor->GetProcess();
	const std::vector<ToyEtch::FSample>& Samples = P.Samples;
	const FToyEtchContent& Content = RigActor->GetContent();
	const FPainter Paint{ AllottedGeometry, OutDrawElements, LayerId, &White };

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float W = static_cast<float>(Size.X), H = static_cast<float>(Size.Y);
	const float Left = FMath::Min(240.f, W * 0.22f);
	const int32 Rows = UE_ARRAY_COUNT(SeriesSpecs);
	const float RowH = (H - 30.f) / Rows;
	// 横轴：整个工艺，至少 120 秒
	const double Now = Samples.empty() ? 0.0 : Samples.back().T;
	const double Window = FMath::Max(120.0, Now);
	const float PlotW = W - Left;
	// 每个像素最多一个点，样本多了跳着取
	const int32 Stride = FMath::Max(1, static_cast<int32>(Samples.size() / FMath::Max(1.f, PlotW)));

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const FSeriesSpec& Spec = SeriesSpecs[Row];
		const float Y0 = Row * RowH;
		const float Y1 = Y0 + RowH - 4.f;
		Paint.Box(Left, Y0, PlotW, RowH - 4.f, ChartBack);
		FString Name = Spec.Name;
		FString Unit = Spec.Unit;
		if (const FToyEtchSeriesText* Text = Content.Series.FindByPredicate([&Spec](const FToyEtchSeriesText& T) { return T.Key == Spec.Key; }))
		{
			Name = ToyEtchUI::VrText(Text->Name);
			Unit = Text->Unit;
		}
		const double Current = Samples.empty() ? (Row == 0 ? 760.0 : 0.0) : SampleValue(Samples.back(), Row);
		Paint.Text(4.f, Y0 + 1.f, Name, Font(15), DimTextColor);
		Paint.Text(4.f, Y0 + 20.f, FString::Printf(TEXT("%s %s"), *FormatValue(Current, Row), *Unit), Font(17, true), TextColor);

		auto YOf = [&Spec, Y0, Y1](double Value)
		{
			double F;
			if (Spec.bLog)
			{
				F = (FMath::LogX(10.0, FMath::Clamp(Value, Spec.Min, Spec.Max)) - FMath::LogX(10.0, Spec.Min)) / (FMath::LogX(10.0, Spec.Max) - FMath::LogX(10.0, Spec.Min));
			}
			else
			{
				F = FMath::Clamp(Value / Spec.Max, 0.0, 1.0);
			}
			return Y1 - 2.f - static_cast<float>(F) * (Y1 - Y0 - 4.f);
		};
		TArray<FVector2f> Points;
		Points.Reserve(static_cast<int32>(Samples.size() / Stride) + 2);
		for (size_t Index = 0; Index < Samples.size(); Index += Stride)
		{
			const ToyEtch::FSample& Sample = Samples[Index];
			Points.Add(FVector2f(Left + static_cast<float>(Sample.T / Window) * PlotW, YOf(SampleValue(Sample, Row))));
		}
		if (!Samples.empty() && (Samples.size() - 1) % Stride != 0)
		{
			const ToyEtch::FSample& Last = Samples.back();
			Points.Add(FVector2f(Left + static_cast<float>(Last.T / Window) * PlotW, YOf(SampleValue(Last, Row))));
		}
		Paint.Lines(MoveTemp(Points), ToyEtchUI::Hex(Spec.Color), 2.f);
	}
	// 终点：红色竖虚线
	if (P.EpT >= 0.0)
	{
		const float X = Left + static_cast<float>(P.EpT / Window) * PlotW;
		const FLinearColor Red = ToyEtchUI::Hex(0xd64545);
		for (float Y = 0.f; Y < H - 30.f; Y += 10.f)
		{
			Paint.Lines({ FVector2f(X, Y), FVector2f(X, FMath::Min(Y + 5.f, H - 30.f)) }, Red, 2.f, 2);
		}
		Paint.Text(X + 5.f, H - 54.f, TEXT("终点"), Font(16, true), Red);
	}
	Paint.Text(Left, H - 26.f, FString::Printf(TEXT("0 ~ %.0f 秒（整个工艺）"), Window), Font(16), DimTextColor);
	return LayerId + 3;
}

// ---------- 平台小地图 ----------

void SToyEtchMap::Construct(const FArguments& InArgs)
{
	Rig = InArgs._Rig;
}

FVector2D SToyEtchMap::ComputeDesiredSize(float) const
{
	return FVector2D(400.f, 270.f);
}

int32 SToyEtchMap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AToyEtcherRig* RigActor = Rig.Get();
	if (!RigActor)
	{
		return LayerId;
	}
	const ToyEtch::FProcess& P = RigActor->GetProcess();
	const FPainter Paint{ AllottedGeometry, OutDrawElements, LayerId, &White };
	const FVector2D Size = AllottedGeometry.GetLocalSize();

	// 平台俯视（米，网页版的布局）：X 向右，Z 朝晶圆盒（画面下方）。范围 X -1.75~1.75，Z -1.7~2.75
	const float Scale = FMath::Min(static_cast<float>(Size.X) / 3.5f, static_cast<float>(Size.Y) / 4.45f);
	const FVector2f Origin(static_cast<float>(Size.X) * 0.5f, (static_cast<float>(Size.Y) - 4.45f * Scale) * 0.5f + 1.7f * Scale);
	auto At = [Scale, Origin](double X, double Z) { return FVector2f(Origin.X + static_cast<float>(X) * Scale, Origin.Y + static_cast<float>(Z) * Scale); };
	auto Rect = [&Paint, &At, Scale](double X, double Z, double W, double D, const FLinearColor& Color, const TCHAR* Label, int32 FontSize)
	{
		const FVector2f Min = At(X - W * 0.5, Z - D * 0.5);
		Paint.Box(Min.X, Min.Y, static_cast<float>(W) * Scale, static_cast<float>(D) * Scale, Color);
		if (Label && *Label)
		{
			Paint.Text(Min.X + 4.f, Min.Y + 2.f, Label, Font(FontSize), FLinearColor(0.85f, 0.9f, 1.f));
		}
	};
	const FLinearColor Station(0.09f, 0.12f, 0.2f);
	const FLinearColor Dim(0.06f, 0.07f, 0.1f);
	const FLinearColor Pm1 = P.S.Plasma > 0.05 ? FMath::Lerp(Station, ToyEtchUI::Hex(0x8f6cff), static_cast<float>(P.S.Plasma) * 0.6f) : FLinearColor(0.16f, 0.12f, 0.3f);
	Rect(0.0, 1.8, 2.2, 0.8, Dim, TEXT("EFEM"), 13);
	Rect(-1.05, -0.05, 0.75, 0.75, Dim, TEXT("PM2"), 13);
	Rect(1.05, -0.05, 0.75, 0.75, Dim, TEXT("PM3"), 13);
	Rect(0.0, 0.0, 1.2, 1.2, Station, TEXT("传输腔"), 14);
	Rect(-0.32, 1.0, 0.5, 0.75, Station, TEXT("负载锁"), 13);
	Rect(0.32, 1.0, 0.5, 0.75, Dim, TEXT(""), 13);
	Rect(0.0, -1.05, 0.85, 0.75, Pm1, TEXT("PM1 刻蚀"), 14);
	Rect(0.70, 1.80, 0.32, 0.32, Station, TEXT(""), 13);
	Rect(-0.55, 2.40, 0.5, 0.42, Station, TEXT("晶圆盒"), 13);
	Paint.Text(At(0.70, 1.80).X - 18.f, At(0.70, 1.80).Y + 0.18f * Scale, TEXT("对准器"), Font(12), DimTextColor);

	// 晶圆：一个亮黄的圆点，外面一圈淡光
	const FVector2D Wafer = RigActor->GetMapDot();
	const FVector2f Center = At(Wafer.X, Wafer.Y);
	const FLinearColor Halo(1.f, 0.85f, 0.3f, 0.3f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(24.f, 24.f), FSlateLayoutTransform(Center - FVector2f(12.f, 12.f))),
		&Dot, ESlateDrawEffect::None, Halo);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(12.f, 12.f), FSlateLayoutTransform(Center - FVector2f(6.f, 6.f))),
		&Dot, ESlateDrawEffect::None, FLinearColor(1.f, 0.85f, 0.3f));
	return LayerId + 3;
}

// ---------- 面板 ----------

TSharedRef<SWidget> ToyEtchUI::MakeResultList(TWeakObjectPtr<AToyEtcherRig> Rig, float Scale, const FSlateBrush* Round)
{
	auto Size = [Scale](float Base) { return FMath::RoundToInt(Base * Scale); };
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	// 总评：合格绿色、不合格红色
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f * Scale)
	[
		SNew(SBorder)
		.BorderImage(Round)
		.Padding(FMargin(16.f * Scale, 10.f * Scale))
		.BorderBackgroundColor_Lambda([Rig]()
		{
			const AToyEtcherRig* R = Rig.Get();
			if (!R || !R->HasResult())
			{
				return FSlateColor(FLinearColor(0.08f, 0.09f, 0.12f));
			}
			return FSlateColor(R->IsVerdictOk() ? FLinearColor(0.04f, 0.2f, 0.08f) : FLinearColor(0.25f, 0.05f, 0.04f));
		})
		[
			SNew(STextBlock)
			.Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->VerdictText() : FText(); })
			.Font(Font(Size(28.f), true))
			.ColorAndOpacity_Lambda([Rig]()
			{
				const AToyEtcherRig* R = Rig.Get();
				return FSlateColor(!R || !R->HasResult() ? DimTextColor : (R->IsVerdictOk() ? OkColor : ErrorColor));
			})
			.AutoWrapText(true)
		]
	];
	// 5 项：√ / × + 名字 + 数值，不合格的下面跟一句原因
	for (int32 Index = 0; Index < 5; ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f * Scale)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([Rig, Index]()
					{
						const AToyEtcherRig* R = Rig.Get();
						if (!R)
						{
							return FText();
						}
						const TCHAR* Mark = !R->HasResult() ? TEXT("·") : (R->IsResultOk(Index) ? TEXT("√") : TEXT("×"));
						return FText::FromString(FString::Printf(TEXT("%s  %s"), Mark, *R->ResultLabel(Index).ToString()));
					})
					.Font(Font(Size(21.f)))
					.ColorAndOpacity(TextColor)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f * Scale, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([Rig, Index]() { return Rig.IsValid() ? Rig->ResultValue(Index) : FText(); })
					.Font(Font(Size(21.f), true))
					.ColorAndOpacity_Lambda([Rig, Index]()
					{
						const AToyEtcherRig* R = Rig.Get();
						return FSlateColor(!R || !R->HasResult() ? DimTextColor : (R->IsResultOk(Index) ? OkColor : ErrorColor));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(28.f * Scale, 2.f * Scale, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text_Lambda([Rig, Index]() { return Rig.IsValid() ? Rig->ResultHint(Index) : FText(); })
				.Font(Font(Size(16.f)))
				.ColorAndOpacity(FLinearColor(1.f, 0.7f, 0.55f))
				.AutoWrapText(true)
				.Visibility_Lambda([Rig, Index]()
				{
					return Rig.IsValid() && !Rig->ResultHint(Index).IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
		];
	}
	List->AddSlot().AutoHeight().Padding(0.f, 6.f * Scale, 0.f, 0.f)
	[
		SNew(STextBlock)
		.Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->ResultStats() : FText(); })
		.Font(Font(Size(15.f)))
		.ColorAndOpacity(DimTextColor)
		.AutoWrapText(true)
	];
	return List;
}

TSharedRef<SWidget> ToyEtchUI::MakeInfoBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B)
{
	// 左边：第几步 + 现在在干什么 + 为什么；右边：平台俯视小图（桌上只有工艺腔，传片那几步靠它看晶圆走到哪了）；下面：整个工艺的趋势曲线
	TSharedRef<SVerticalBox> Text = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->ExplainTitle() : FText(); })
			.Font(Font(34, true))
			.ColorAndOpacity_Lambda([Rig]() { return FSlateColor(Rig.IsValid() && Rig->IsExplainError() ? ErrorColor : TitleColor); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->ExplainNow() : FText(); })
			.Font(Font(25))
			.ColorAndOpacity(TextColor)
			.AutoWrapText(true)
			.LineHeightPercentage(1.1f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([Rig]()
			{
				const FText Why = Rig.IsValid() ? Rig->ExplainWhy() : FText();
				return Why.IsEmpty() ? FText() : FText::FromString(FString::Printf(TEXT("为什么：%s"), *Why.ToString()));
			})
			.Font(Font(21))
			.ColorAndOpacity(DimTextColor)
			.AutoWrapText(true)
			.Visibility_Lambda([Rig]() { return Rig.IsValid() && !Rig->ExplainWhy().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		];
	TSharedRef<SVerticalBox> Map = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("晶圆在哪（平台俯视）"))).Font(Font(17, true)).ColorAndOpacity(DimTextColor)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(&B.Round)
			.BorderBackgroundColor(ChartBack)
			.Padding(6.f)
			[
				SNew(SBox).WidthOverride(268.f).HeightOverride(260.f)
				[
					SNew(SToyEtchMap).Rig(Rig)
				]
			]
		];

	return SNew(SBorder)
		.BorderImage(&B.Flat)
		.BorderBackgroundColor(BackColor)
		.Padding(FMargin(36.f, 24.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 24.f, 0.f)
				[
					Text
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					Map
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 6.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock)
					.Text_Lambda([Rig]()
					{
						return FText::FromString(FString::Printf(TEXT("工艺曲线 · %s"), Rig.IsValid() ? *Rig->StatusLine().ToString() : TEXT("")));
					})
					.Font(Font(20, true))
					.ColorAndOpacity(DimTextColor)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->EndpointText() : FText(); })
					.Font(Font(20, true))
					.ColorAndOpacity(Hex(0xff6b6b))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(300.f)
				[
					SNew(SToyEtchTrend).Rig(Rig)
				]
			]
		];
}

TSharedRef<SWidget> ToyEtchUI::MakeProfileBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B)
{
	// 图例（网页版剖面图底下那一行）
	TSharedRef<SHorizontalBox> Legend = SNew(SHorizontalBox);
	struct FLegendEntry
	{
		uint32 Color;
		const TCHAR* Name;
	};
	const FLegendEntry Items[] = {
		{ 0xc4455a, TEXT("光刻胶") }, { 0xc6a86c, TEXT("多晶硅") }, { 0xc9d6f0, TEXT("栅氧（放大两倍）") },
		{ 0x6b7487, TEXT("硅") }, { 0x7fd8ff, TEXT("离子") }, { 0xffb347, TEXT("自由基") },
	};
	for (const FLegendEntry& Item : Items)
	{
		Legend->AddSlot().AutoWidth().Padding(0.f, 0.f, 18.f, 0.f)
		[
			LegendItem(&B.Round, Hex(Item.Color), Item.Name, 17)
		];
	}
	return SNew(SBorder)
		.BorderImage(&B.Flat)
		.BorderBackgroundColor(BackColor)
		.Padding(FMargin(24.f, 18.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("剖面 · 晶圆上的一根栅极"))).Font(Font(28, true)).ColorAndOpacity(TitleColor)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("宽 60 nm、高 100 nm 的多晶硅线条；1 nm 画成约 1 mm（放大一百万倍）")))
				.Font(Font(17)).ColorAndOpacity(DimTextColor).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 6.f)
			[
				SNew(STextBlock).Text_Lambda([Rig]() { return Rig.IsValid() ? Rig->DepthText() : FText(); }).Font(Font(21, true)).ColorAndOpacity(TextColor)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SToyEtchProfile).Rig(Rig)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				Legend
			]
		];
}

TSharedRef<SWidget> ToyEtchUI::MakeResultBoard(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B)
{
	return SNew(SBorder)
		.BorderImage(&B.Flat)
		.BorderBackgroundColor(BackColor)
		.Padding(FMargin(28.f, 22.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("这一片的结果"))).Font(Font(24, true)).ColorAndOpacity(DimTextColor)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				MakeResultList(Rig, 1.f, &B.Round)
			]
		];
}

TSharedRef<SWidget> ToyEtchUI::MakeLegend(TWeakObjectPtr<AToyEtcherRig> Rig, const FToyEtchBrushes& B)
{
	// 粒子名字从讲解文字里取（和网页一样），没读到用这里的
	static const TCHAR* const Keys[] = { TEXT("gas"), TEXT("e"), TEXT("ion"), TEXT("rad"), TEXT("prod") };
	static const TCHAR* const Fallback[] = { TEXT("气体分子（Cl2 / HBr）"), TEXT("电子"), TEXT("离子（Cl+ 等）"), TEXT("自由基（Cl·）"), TEXT("产物 SiCl4") };
	auto Name = [&Rig](int32 Type)
	{
		const AToyEtcherRig* R = Rig.Get();
		const FString* Text = R ? R->GetContent().Particles.Find(Keys[Type]) : nullptr;
		return Text ? VrText(*Text) : FString(Fallback[Type]);
	};
	auto Row = [&B, &Name](std::initializer_list<int32> Types)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (const int32 Type : Types)
		{
			Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 22.f, 0.f)
			[
				LegendItem(&B.Dot, ParticleColor(Type), Name(Type), 17)
			];
		}
		return Box;
	};
	return SNew(SBorder)
		.BorderImage(&B.Flat)
		.BorderBackgroundColor(BackColor)
		.Padding(FMargin(16.f, 8.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("放大的反应腔（真实尺寸的 60%，粒子只是示意）"))).Font(Font(16)).ColorAndOpacity(DimTextColor)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				Row({ 0, 1, 2 })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				Row({ 3, 4 })
			]
		];
}
