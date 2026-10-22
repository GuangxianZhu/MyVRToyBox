#include "ToyTouchScreen.h"

#include "ToyShapeComponent.h"
#include "ToyStudio.h"
#include "Algo/Find.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"

/** 界面用到的画刷。Slate 控件只存画刷的指针，所以画刷要比界面活得久：跟着触屏 Actor 走。 */
struct FToyScreenBrushes
{
	FSlateColorBrush Flat = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush Panel = FSlateRoundedBoxBrush(FLinearColor::White, 22.f);
	FSlateRoundedBoxBrush Card = FSlateRoundedBoxBrush(FLinearColor::White, 18.f);
	FSlateRoundedBoxBrush Swatch = FSlateRoundedBoxBrush(FLinearColor::White, 12.f);
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 100.f);
};

namespace
{
	const FLinearColor StandColor(0.03f, 0.03f, 0.035f);
	const FLinearColor DisplayColor(0.02f, 0.05f, 0.12f);

	// 屏幕 32 × 22 cm，界面按每厘米 40 像素渲染（1280 × 880）。头显在 50cm 外每厘米只有约 29 个像素，再高也看不出区别
	constexpr float ScreenW = 32.f;
	constexpr float ScreenH = 22.f;
	constexpr float PxPerCm = 40.f;
	const FVector2D DrawSize(ScreenW * PxPerCm, ScreenH * PxPerCm);
	// 界面面板贴在显示屏表面前 0.05cm（显示屏前表面在屏幕坐标 X = 1.4）
	constexpr float SurfaceX = 1.45f;

	// 界面颜色按"看起来的样子"写（0~1），整体再乘 Brightness。字是白的，按钮在 0.3 上下，背景接近黑
	const FLinearColor BackColor(0.02f, 0.03f, 0.06f);
	const FLinearColor PanelColor(0.045f, 0.06f, 0.1f);
	const FLinearColor TextColor(1.f, 1.f, 1.f);
	const FLinearColor DimTextColor(0.55f, 0.65f, 0.8f);
	const FLinearColor WarnColor(1.f, 0.55f, 0.15f);
	const FLinearColor SelectColor(1.f, 0.8f, 0.3f);
	const FLinearColor DisabledColor(0.07f, 0.075f, 0.085f);
	const FLinearColor GreyButton(0.16f, 0.17f, 0.2f);
	const FLinearColor MoveButton(0.08f, 0.13f, 0.2f);

	/** 七个模型卡片：模型、按钮动作、中文名、英文代号、导入脚本名（scripts/import_*.bat）、颜色。 */
	struct FToyModelEntry
	{
		EToyModelKind Kind;
		EToyScreenAction Action;
		const TCHAR* Title;
		const TCHAR* Code;
		const TCHAR* Script;
		FLinearColor Color;
	};

	const FToyModelEntry ModelTable[] = {
		{ EToyModelKind::Motor, EToyScreenAction::ToggleMotor, TEXT("三相电机"), TEXT("MOTOR"), TEXT("motor"), FLinearColor(0.15f, 0.35f, 0.85f) },
		{ EToyModelKind::Valve, EToyScreenAction::ToggleValve, TEXT("气动阀门"), TEXT("VALVE"), TEXT("valve"), FLinearColor(0.1f, 0.55f, 0.6f) },
		{ EToyModelKind::Drone, EToyScreenAction::ToggleDrone, TEXT("涵道无人机"), TEXT("DRONE"), TEXT("drone"), FLinearColor(0.45f, 0.3f, 0.75f) },
		{ EToyModelKind::Lathe, EToyScreenAction::ToggleLathe, TEXT("迷你车床"), TEXT("LATHE"), TEXT("lathe"), FLinearColor(0.7f, 0.25f, 0.45f) },
		{ EToyModelKind::Multimeter, EToyScreenAction::ToggleMultimeter, TEXT("数字万用表"), TEXT("METER"), TEXT("meter"), FLinearColor(0.85f, 0.65f, 0.05f) },
		{ EToyModelKind::Server, EToyScreenAction::ToggleServer, TEXT("AI 服务器"), TEXT("SERVER"), TEXT("server"), FLinearColor(0.3f, 0.6f, 0.1f) },
		{ EToyModelKind::Orb, EToyScreenAction::ToggleOrb, TEXT("漂浮机器人"), TEXT("ORB-1"), TEXT("orb"), FLinearColor(0.95f, 0.4f, 0.05f) },
	};
	constexpr int32 ModelCount = UE_ARRAY_COUNT(ModelTable);

	// 模型卡片 4 × 2，每页 7 个模型 + 最后一格"收起模型"。卡片 190 × 220 像素 ≈ 4.8 × 5.5cm
	constexpr int32 ModelsPerPage = 7;
	const FVector2D CardOrigin(30.f, 150.f);
	const FVector2D CardSize(190.f, 220.f);
	constexpr float CardGap = 20.f;

	FBox2D PixelRect(double X, double Y, double W, double H)
	{
		return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	}

	FBox2D CardRect(int32 Slot)
	{
		const FVector2D Min = CardOrigin + FVector2D((Slot % 4) * (CardSize.X + CardGap), (Slot / 4) * (CardSize.Y + CardGap));
		return FBox2D(Min, Min + CardSize);
	}

	FLinearColor Scaled(const FLinearColor& Color, float Scale)
	{
		return FLinearColor(Color.R * Scale, Color.G * Scale, Color.B * Scale, Color.A);
	}

	FSlateFontInfo Font(int32 Size, bool bBold = true)
	{
		// 引擎默认字体带中文后备字形（信息牌也是用它）
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** 把触屏的一个 const 成员函数包成 Slate 每帧读值的 lambda；触屏没了就返回 Fallback。 */
	template <typename R, typename... TParams, typename... TArgs>
	auto Live(TWeakObjectPtr<AToyTouchScreen> Weak, R (AToyTouchScreen::*Getter)(TParams...) const, R Fallback, TArgs... Args)
	{
		return [Weak, Getter, Fallback, Args...]() -> R
		{
			const AToyTouchScreen* Screen = Weak.Get();
			return Screen ? (Screen->*Getter)(Args...) : Fallback;
		};
	}
}

AToyTouchScreen::AToyTouchScreen()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	UToyShapeComponent::Create(this, TEXT("Base"), Root, EToyShape::Cylinder, FVector(14.f, 14.f, 1.5f),
		FVector(0.f, 0.f, 0.75f), StandColor, FRotator::ZeroRotator);
	UToyShapeComponent::Create(this, TEXT("Pole"), Root, EToyShape::Cylinder, FVector(2.5f, 2.5f, 26.f),
		FVector(-3.f, 0.f, 14.f), StandColor, FRotator::ZeroRotator);

	// Pitch 30：屏幕往后仰，正面朝向站着的人的视线
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Pivot->SetRelativeLocationAndRotation(FVector(-2.f, 0.f, 28.f), FRotator(30.f, 0.f, 0.f));

	UToyShapeComponent::Create(this, TEXT("Bezel"), Pivot, EToyShape::Box, FVector(2.f, ScreenW + 2.f, ScreenH + 2.f),
		FVector::ZeroVector, StandColor, FRotator::ZeroRotator);
	// 显示屏底板：编辑器视口里（界面还没生成）看到的就是它
	UToyShapeComponent::Create(this, TEXT("Display"), Pivot, EToyShape::Box, FVector(0.4f, ScreenW, ScreenH),
		FVector(1.2f, 0.f, 0.f), DisplayColor, FRotator::ZeroRotator);

	// 界面面板：WidgetComponent 的本地单位是像素，缩放 1/40 → 1 像素 = 0.025cm，正好铺满 32 × 22cm。面朝 +X
	Gui = CreateDefaultSubobject<UWidgetComponent>(TEXT("Gui"));
	Gui->SetupAttachment(Pivot);
	Gui->SetRelativeLocation(FVector(SurfaceX, 0.f, 0.f));
	Gui->SetRelativeScale3D(FVector(1.f / PxPerCm));
	Gui->SetWidgetSpace(EWidgetSpace::World);
	Gui->SetDrawSize(DrawSize);
	Gui->SetDrawAtDesiredSize(false);
	Gui->SetPivot(FVector2D(0.5f, 0.5f));
	Gui->SetTwoSided(false);
	Gui->SetBlendMode(EWidgetBlendMode::Opaque);
	Gui->SetWindowFocusable(false);
	Gui->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Gui->SetCastShadow(false);

	// 感应区：屏幕表面后 1.5cm 到前 3.5cm。手每帧在指尖做小球重叠查询（AToyHand::TickPoke），查到它才把指尖交给 TouchFinger
	TouchZone = CreateDefaultSubobject<UBoxComponent>(TEXT("TouchZone"));
	TouchZone->SetupAttachment(Pivot);
	TouchZone->SetRelativeLocation(FVector(SurfaceX + 1.f, 0.f, 0.f));
	TouchZone->InitBoxExtent(FVector(2.5f, ScreenW * 0.5f, ScreenH * 0.5f));
	TouchZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TouchZone->SetCollisionObjectType(ECC_WorldDynamic);
	TouchZone->SetCollisionResponseToAllChannels(ECR_Overlap);
	TouchZone->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	TouchZone->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// 鼠标点击用的薄片：就在屏幕表面，射线打到的点几乎没有视差
	ClickPlane = CreateDefaultSubobject<UBoxComponent>(TEXT("ClickPlane"));
	ClickPlane->SetupAttachment(Pivot);
	ClickPlane->SetRelativeLocation(FVector(SurfaceX, 0.f, 0.f));
	ClickPlane->InitBoxExtent(FVector(0.05f, ScreenW * 0.5f, ScreenH * 0.5f));
	ClickPlane->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickPlane->SetCollisionObjectType(ECC_WorldStatic);
	ClickPlane->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickPlane->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	BuildItems();
}

void AToyTouchScreen::BeginPlay()
{
	Super::BeginPlay();

	// L_Studio 是旧版触屏（3D 按钮）时存的：子 Actor 模板里还带着旧的 *Face / *Line / *Zone / Status 组件，
	// 读回来缩放都是 1 —— *Face 变成 1m 的白板挡在桌子右边，*Zone 挡住鼠标射线。构造函数没建的组件一律删掉
	static const FName KeepShapes[] = { TEXT("Base"), TEXT("Pole"), TEXT("Bezel"), TEXT("Display") };
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		const bool bKeep = Primitive == Gui || Primitive == TouchZone || Primitive == ClickPlane
			|| Algo::Find(KeepShapes, Primitive->GetFName()) != nullptr;
		if (!bKeep)
		{
			UE_LOG(LogTemp, Log, TEXT("ToyBox: 触屏删掉关卡里存的旧组件 %s"), *Primitive->GetName());
			Primitive->DestroyComponent();
		}
	}

	Brushes = MakeShared<FToyScreenBrushes>();
	Gui->SetTintColorAndOpacity(FLinearColor(Brightness, Brightness, Brightness, 1.f));
	Gui->SetSlateWidget(BuildGui());
}

void AToyTouchScreen::SetStudio(AToyStudio* InStudio)
{
	Studio = InStudio;
}

void AToyTouchScreen::BuildItems()
{
	// 布局（像素，界面 1280 × 880，左上角为原点）：
	//   顶栏 0~90：标题、提示、右上角菜单
	//   左 30~850：模型卡片 4 × 2（150~630），方块两个大按钮（700~840）
	//   右 870~1260：当前模型（爆炸 / 复位）和挪动方向键
	Items.Reset();
	auto Add = [this](EToyScreenAction Action, const FBox2D& Rect, const FLinearColor& Color, int32 InPage = INDEX_NONE, int32 InModel = INDEX_NONE)
	{
		FToyScreenItem& Item = Items.AddDefaulted_GetRef();
		Item.Action = Action;
		Item.Rect = Rect;
		Item.Color = Color;
		Item.Page = InPage;
		Item.Model = InModel;
	};

	Add(EToyScreenAction::OpenMenu, PixelRect(1090.f, 18.f, 170.f, 60.f), GreyButton);

	for (int32 Index = 0; Index < ModelCount; ++Index)
	{
		Add(ModelTable[Index].Action, CardRect(Index % ModelsPerPage), Scaled(ModelTable[Index].Color, 0.22f), Index / ModelsPerPage, Index);
	}
	Add(EToyScreenAction::HideModel, CardRect(ModelsPerPage), GreyButton);
	// 翻页箭头：模型超过 7 个才出现（IsItemVisible）
	Add(EToyScreenAction::PrevPage, PixelRect(690.f, 96.f, 74.f, 46.f), GreyButton);
	Add(EToyScreenAction::NextPage, PixelRect(776.f, 96.f, 74.f, 46.f), GreyButton);

	Add(EToyScreenAction::SpawnCube, PixelRect(30.f, 700.f, 400.f, 140.f), FLinearColor(0.04f, 0.25f, 0.1f));
	Add(EToyScreenAction::ClearCubes, PixelRect(450.f, 700.f, 400.f, 140.f), FLinearColor(0.3f, 0.06f, 0.05f));

	Add(EToyScreenAction::ToggleExplode, PixelRect(890.f, 236.f, 350.f, 120.f), FLinearColor(0.34f, 0.2f, 0.02f));
	Add(EToyScreenAction::ResetParts, PixelRect(890.f, 372.f, 350.f, 96.f), GreyButton);

	// 方向键 110 × 100（≈ 2.8 × 2.5cm），中间一格显示距离
	Add(EToyScreenAction::MoveFar, PixelRect(1010.f, 530.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveLeft, PixelRect(890.f, 640.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveRight, PixelRect(1130.f, 640.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveNear, PixelRect(1010.f, 750.f, 110.f, 100.f), MoveButton);
}

TSharedRef<SWidget> AToyTouchScreen::BuildGui()
{
	const TWeakObjectPtr<AToyTouchScreen> Weak(this);
	const FToyScreenBrushes& B = *Brushes;
	TSharedRef<SCanvas> Canvas = SNew(SCanvas);

	auto AddBox = [&Canvas](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		Canvas->AddSlot().Position(Rect.Min).Size(Rect.GetSize())
		[
			SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(Color)
		];
	};
	// SCanvas 槽的 HAlign / VAlign 是"控件相对 Position 这个点怎么对齐"（Center = 以 Position 为中心），不是在 Size 里居中。
	// 槽一律左上对齐，在 Rect 里的对齐交给里面的 SBox
	auto AddWidget = [&Canvas](const FBox2D& Rect, EHorizontalAlignment HAlign, const TSharedRef<SWidget>& Widget)
	{
		Canvas->AddSlot().Position(Rect.Min).Size(Rect.GetSize())
		[
			SNew(SBox).HAlign(HAlign).VAlign(VAlign_Center)
			[
				Widget
			]
		];
	};
	auto AddText = [&AddWidget](const FBox2D& Rect, TAttribute<FText> Text, int32 Size, const FLinearColor& Color, EHorizontalAlignment HAlign)
	{
		AddWidget(Rect, HAlign, SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color));
	};

	// 背景和两块面板
	AddBox(PixelRect(0.f, 0.f, DrawSize.X, DrawSize.Y), &B.Flat, BackColor);
	AddBox(PixelRect(870.f, 96.f, 390.f, 764.f), &B.Panel, PanelColor);

	// 顶栏：标题，右边跟一行提示（缺模型时变成橙色的导入提示）
	AddText(PixelRect(30.f, 18.f, 300.f, 60.f), FText::FromString(TEXT("VR 玩具箱")), 30, TextColor, HAlign_Left);
	AddWidget(PixelRect(260.f, 18.f, 810.f, 60.f), HAlign_Left,
		SNew(STextBlock)
		.Text_Lambda(Live(Weak, &AToyTouchScreen::StatusText, FText()))
		.Font(Font(16, false))
		.ColorAndOpacity_Lambda([Weak]()
		{
			const AToyStudio* StudioActor = Weak.IsValid() ? Weak->Studio.Get() : nullptr;
			return FSlateColor(StudioActor && StudioActor->GetMissingKind() ? WarnColor : DimTextColor);
		}));

	// 左边的小标题
	AddText(PixelRect(30.f, 96.f, 300.f, 46.f), FText::FromString(TEXT("模型")), 20, DimTextColor, HAlign_Left);
	AddText(PixelRect(30.f, 646.f, 820.f, 46.f), TAttribute<FText>::CreateLambda(Live(Weak, &AToyTouchScreen::CubeText, FText())), 20, DimTextColor, HAlign_Left);

	// 右边：当前模型的名字和状态、挪动标题、方向键中间的距离
	AddText(PixelRect(890.f, 104.f, 350.f, 34.f), FText::FromString(TEXT("当前模型")), 16, DimTextColor, HAlign_Left);
	AddText(PixelRect(890.f, 136.f, 350.f, 50.f), TAttribute<FText>::CreateLambda(Live(Weak, &AToyTouchScreen::ModelTitle, FText())), 28, TextColor, HAlign_Left);
	AddWidget(PixelRect(890.f, 186.f, 350.f, 40.f), HAlign_Left,
		SNew(STextBlock).Text_Lambda(Live(Weak, &AToyTouchScreen::ModelState, FText())).Font(Font(18, false)).ColorAndOpacity(DimTextColor));
	AddText(PixelRect(890.f, 482.f, 350.f, 40.f), FText::FromString(TEXT("挪动模型")), 18, DimTextColor, HAlign_Left);
	AddWidget(PixelRect(1010.f, 640.f, 110.f, 100.f), HAlign_Center,
		SNew(STextBlock).Text_Lambda(Live(Weak, &AToyTouchScreen::DistanceText, FText())).Font(Font(18)).ColorAndOpacity(DimTextColor)
		.Justification(ETextJustify::Center));

	// 按钮和卡片：外层是选中描边（不选中时透明），内层是按钮底色；按下的一瞬间缩小 5%
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const FToyScreenItem& Item = Items[Index];
		const bool bCard = Item.Model != INDEX_NONE || Item.Action == EToyScreenAction::HideModel;
		Canvas->AddSlot().Position(Item.Rect.Min).Size(Item.Rect.GetSize())
		[
			SNew(SBorder)
			.BorderImage(bCard ? &B.Card : &B.Swatch)
			.Padding(4.f)
			.BorderBackgroundColor_Lambda([Weak, Index]()
			{
				return FSlateColor(Weak.IsValid() && Weak->IsItemSelected(Index) ? SelectColor : FLinearColor::Transparent);
			})
			.Visibility_Lambda([Weak, Index]()
			{
				return Weak.IsValid() && Weak->IsItemVisible(Index) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.RenderTransform_Lambda([Weak, Index]()
			{
				const bool bFlash = Weak.IsValid() && Weak->IsItemFlashing(Index);
				return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FScale2f(bFlash ? 0.95f : 1.f)));
			})
			[
				SNew(SBorder)
				.BorderImage(bCard ? &B.Card : &B.Swatch)
				.BorderBackgroundColor_Lambda(Live(Weak, &AToyTouchScreen::ItemColor, FSlateColor(DisabledColor), Index))
				.Padding(FMargin(10.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					BuildItemContent(Index)
				]
			]
		];
	}

	// 手指光标：指尖投影的位置画一个圆点，越靠近屏幕越小，按下时变成橙色
	for (int32 Slot = 0; Slot < static_cast<int32>(UE_ARRAY_COUNT(Fingers)); ++Slot)
	{
		auto Radius = [Weak, Slot]()
		{
			const float Distance = Weak.IsValid() ? Weak->Fingers[Slot].Distance : 0.f;
			const float Hover = Weak.IsValid() ? Weak->HoverCm : 1.f;
			return FMath::Lerp(9.f, 24.f, FMath::Clamp(Distance / Hover, 0.f, 1.f));
		};
		Canvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([Weak, Slot, Radius]()
		{
			const FVector2D Pixel = Weak.IsValid() ? Weak->Fingers[Slot].Pixel : FVector2D::ZeroVector;
			return Pixel - FVector2D(Radius());
		}))
		.Size(TAttribute<FVector2D>::CreateLambda([Radius]() { return FVector2D(Radius() * 2.f); }))
		[
			SNew(SBorder)
			.BorderImage(&B.Dot)
			.BorderBackgroundColor_Lambda([Weak, Slot]()
			{
				const bool bPressed = Weak.IsValid() && Weak->Fingers[Slot].bPressed;
				return FSlateColor(bPressed ? FLinearColor(1.f, 0.5f, 0.1f, 0.9f) : FLinearColor(1.f, 1.f, 1.f, 0.55f));
			})
			.Visibility_Lambda([Weak, Slot]()
			{
				if (!Weak.IsValid())
				{
					return EVisibility::Collapsed;
				}
				const FToyScreenFinger& Finger = Weak->Fingers[Slot];
				const bool bShow = Finger.bActive && !Finger.bMouse && Finger.Distance < Weak->HoverCm && Finger.Distance > -1.f;
				return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		];
	}

	return Canvas;
}

TSharedRef<SWidget> AToyTouchScreen::BuildItemContent(int32 Index)
{
	const TWeakObjectPtr<AToyTouchScreen> Weak(this);
	const FToyScreenItem& Item = Items[Index];
	auto TextColorOf = [Weak, Index]()
	{
		return FSlateColor(Weak.IsValid() && !Weak->IsItemEnabled(Index) ? FLinearColor(0.4f, 0.42f, 0.46f) : TextColor);
	};

	if (Item.Model != INDEX_NONE)
	{
		// 模型卡片：上面一块模型颜色的色块（以后换成缩略图），下面中文名和状态
		const FToyModelEntry& Entry = ModelTable[Item.Model];
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(166.f).HeightOverride(100.f)
				[
					SNew(SBorder)
					.BorderImage(&Brushes->Swatch)
					.BorderBackgroundColor(Scaled(Entry.Color, 0.6f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Entry.Code)).Font(Font(20)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.85f))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromString(Entry.Title)).Font(Font(22)).ColorAndOpacity(TextColor)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text_Lambda(Live(Weak, &AToyTouchScreen::ItemLabel, FText(), Index)).Font(Font(14, false)).ColorAndOpacity(DimTextColor)
			];
	}

	const int32 Size = Item.Rect.GetSize().Y >= 110.f ? 28 : 22;
	return SNew(STextBlock)
		.Text_Lambda(Live(Weak, &AToyTouchScreen::ItemLabel, FText(), Index))
		.Font(Font(Size))
		.Justification(ETextJustify::Center)
		.ColorAndOpacity_Lambda(TextColorOf);
}

int32 AToyTouchScreen::PageCount() const
{
	return FMath::Max(1, FMath::DivideAndRoundUp(ModelCount, ModelsPerPage));
}

bool AToyTouchScreen::IsItemVisible(int32 Index) const
{
	const FToyScreenItem& Item = Items[Index];
	if (Item.Action == EToyScreenAction::PrevPage || Item.Action == EToyScreenAction::NextPage)
	{
		return PageCount() > 1;
	}
	return Item.Page == INDEX_NONE || Item.Page == Page;
}

bool AToyTouchScreen::IsItemEnabled(int32 Index) const
{
	const AToyStudio* StudioActor = Studio.Get();
	switch (Items[Index].Action)
	{
	case EToyScreenAction::HideModel:
	case EToyScreenAction::ResetParts:
		return StudioActor && StudioActor->HasModel();
	case EToyScreenAction::PrevPage:
		return Page > 0;
	case EToyScreenAction::NextPage:
		return Page < PageCount() - 1;
	default:
		return true;
	}
}

bool AToyTouchScreen::IsItemHovered(int32 Index) const
{
	for (const FToyScreenFinger& Finger : Fingers)
	{
		if (Finger.bActive && Finger.Hover == Index)
		{
			return true;
		}
	}
	return false;
}

bool AToyTouchScreen::IsItemSelected(int32 Index) const
{
	const AToyStudio* StudioActor = Studio.Get();
	const int32 Model = Items[Index].Model;
	return StudioActor && Model != INDEX_NONE && StudioActor->IsModelShown(ModelTable[Model].Kind);
}

bool AToyTouchScreen::IsItemFlashing(int32 Index) const
{
	return Index == FlashIndex && FPlatformTime::Seconds() < FlashUntil;
}

FSlateColor AToyTouchScreen::ItemColor(int32 Index) const
{
	if (!IsItemEnabled(Index))
	{
		return FSlateColor(DisabledColor);
	}
	const FLinearColor Base = Items[Index].Color;
	if (IsItemFlashing(Index))
	{
		return FSlateColor(FMath::Lerp(Base, FLinearColor::White, 0.6f));
	}
	// 悬停：亮一截，告诉你手指正对着它
	return FSlateColor(IsItemHovered(Index) ? Scaled(Base, 1.8f) + FLinearColor(0.04f, 0.04f, 0.04f, 0.f) : Base);
}

int32 AToyTouchScreen::HitTest(const FVector2D& Pixel) const
{
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (IsItemVisible(Index) && Items[Index].Rect.IsInside(Pixel))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FToyScreenFinger* AToyTouchScreen::FindFinger(const UObject* FingerOwner, bool bCreate)
{
	for (FToyScreenFinger& Finger : Fingers)
	{
		if (Finger.Owner == FingerOwner)
		{
			return &Finger;
		}
	}
	if (!bCreate)
	{
		return nullptr;
	}
	for (FToyScreenFinger& Finger : Fingers)
	{
		if (!Finger.bActive)
		{
			Finger = FToyScreenFinger();
			Finger.Owner = FingerOwner;
			return &Finger;
		}
	}
	return nullptr;
}

void AToyTouchScreen::UpdateFinger(FToyScreenFinger& Finger, const FVector& WorldPoint)
{
	// 离屏幕多远：在屏幕自己的坐标系（Pivot，没有缩放）里量 X；投影到界面像素交给 WidgetComponent 算，保证和画面一致
	const FVector Local = Pivot->GetComponentTransform().InverseTransformPosition(WorldPoint);
	// 刚进感应区的第一帧没有"上一帧"：当它是贴着屏幕进来的，不算按下
	Finger.LastDistance = Finger.bActive ? Finger.Distance : -100.f;
	Finger.Distance = static_cast<float>(Local.X) - SurfaceX;
	FVector2D Pixel;
	Gui->GetLocalHitLocation(WorldPoint, Pixel);
	Finger.Pixel = Pixel;
	Finger.bActive = true;
}

bool AToyTouchScreen::TouchFinger(const UObject* Finger, const FVector& TipWorld)
{
	FToyScreenFinger* State = FindFinger(Finger, true);
	if (!State)
	{
		return false;
	}
	UpdateFinger(*State, TipWorld);
	State->Hover = State->Distance < HoverCm ? HitTest(State->Pixel) : INDEX_NONE;

	if (State->bPressed)
	{
		// 按下以后要退回 ReleaseCm 以外才算松开，手指在 0.6cm 附近抖也不会连点
		if (State->Distance > ReleaseCm)
		{
			State->bPressed = false;
		}
		return false;
	}
	// 只认从前面戳进来的：上一帧还在 PressCm 外面。手从侧面或屏幕后面滑进来、一开始就贴着屏幕，都不算
	if (State->Distance <= PressCm && State->LastDistance > PressCm)
	{
		State->bPressed = true;
		return State->Hover != INDEX_NONE && Press(State->Hover);
	}
	return false;
}

void AToyTouchScreen::ReleaseFinger(const UObject* Finger)
{
	if (FToyScreenFinger* State = FindFinger(Finger, false))
	{
		*State = FToyScreenFinger();
	}
}

bool AToyTouchScreen::MouseAt(const FVector& WorldPoint, bool bClick)
{
	FToyScreenFinger* State = FindFinger(this, true);
	if (!State)
	{
		return false;
	}
	UpdateFinger(*State, WorldPoint);
	State->bMouse = true;
	State->Hover = HitTest(State->Pixel);
	return bClick && State->Hover != INDEX_NONE && Press(State->Hover);
}

void AToyTouchScreen::MouseLeave()
{
	ReleaseFinger(this);
}

bool AToyTouchScreen::Press(int32 Index)
{
	if (!IsItemVisible(Index) || !IsItemEnabled(Index))
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPressTime < PressCooldown)
	{
		return false;
	}
	LastPressTime = Now;
	FlashIndex = Index;
	FlashUntil = Now + 0.15;

	const EToyScreenAction Action = Items[Index].Action;
	if (Action == EToyScreenAction::PrevPage || Action == EToyScreenAction::NextPage)
	{
		Page = FMath::Clamp(Page + (Action == EToyScreenAction::NextPage ? 1 : -1), 0, PageCount() - 1);
	}
	else if (AToyStudio* StudioActor = Studio.Get())
	{
		StudioActor->HandleScreenAction(Action);
	}
	return true;
}

FText AToyTouchScreen::StatusText() const
{
	const AToyStudio* StudioActor = Studio.Get();
	if (StudioActor && StudioActor->GetMissingKind())
	{
		const EToyModelKind Missing = *StudioActor->GetMissingKind();
		for (const FToyModelEntry& Entry : ModelTable)
		{
			if (Entry.Kind == Missing)
			{
				return FText::FromString(FString::Printf(TEXT("%s 还没导入：先运行 scripts/import_%s.bat"), Entry.Title, Entry.Script));
			}
		}
	}
	return FText::FromString(TEXT("伸出食指，轻点按钮"));
}

FText AToyTouchScreen::ModelTitle() const
{
	const AToyStudio* StudioActor = Studio.Get();
	if (StudioActor && StudioActor->HasModel())
	{
		for (const FToyModelEntry& Entry : ModelTable)
		{
			if (StudioActor->IsModelShown(Entry.Kind))
			{
				return FText::FromString(Entry.Title);
			}
		}
	}
	return FText::FromString(TEXT("桌上没有模型"));
}

FText AToyTouchScreen::ModelState() const
{
	const AToyStudio* StudioActor = Studio.Get();
	if (!StudioActor || !StudioActor->HasModel())
	{
		return FText::FromString(TEXT("点左边的卡片摆一个出来"));
	}
	return FText::FromString(StudioActor->IsModelExploded() ? TEXT("爆炸图 · 零件可以拿起来看") : TEXT("装配状态"));
}

FText AToyTouchScreen::CubeText() const
{
	const AToyStudio* StudioActor = Studio.Get();
	return FText::FromString(StudioActor
		? FString::Printf(TEXT("方块  %d / %d"), StudioActor->GetCubeCount(), StudioActor->MaxCubes)
		: FString(TEXT("方块")));
}

FText AToyTouchScreen::DistanceText() const
{
	const AToyStudio* StudioActor = Studio.Get();
	return FText::FromString(StudioActor ? FString::Printf(TEXT("离你\n%.0f cm"), StudioActor->GetModelDistanceCm()) : FString());
}

FText AToyTouchScreen::ItemLabel(int32 Index) const
{
	const AToyStudio* StudioActor = Studio.Get();
	const FToyScreenItem& Item = Items[Index];
	if (Item.Model != INDEX_NONE)
	{
		const bool bShown = StudioActor && StudioActor->IsModelShown(ModelTable[Item.Model].Kind);
		return FText::FromString(bShown ? TEXT("● 在桌上") : TEXT("点一下摆出来"));
	}
	switch (Item.Action)
	{
	case EToyScreenAction::OpenMenu:      return FText::FromString(TEXT("菜单"));
	case EToyScreenAction::HideModel:     return FText::FromString(TEXT("收起模型"));
	case EToyScreenAction::PrevPage:      return FText::FromString(TEXT("上页"));
	case EToyScreenAction::NextPage:      return FText::FromString(TEXT("下页"));
	case EToyScreenAction::SpawnCube:     return FText::FromString(TEXT("生成方块"));
	case EToyScreenAction::ClearCubes:    return FText::FromString(TEXT("清空方块"));
	case EToyScreenAction::ToggleExplode: return FText::FromString(StudioActor && StudioActor->IsModelExploded() ? TEXT("组装回去") : TEXT("爆炸拆开"));
	case EToyScreenAction::ResetParts:    return FText::FromString(TEXT("零件复位"));
	case EToyScreenAction::MoveFar:       return FText::FromString(TEXT("推远"));
	case EToyScreenAction::MoveNear:      return FText::FromString(TEXT("拉近"));
	case EToyScreenAction::MoveLeft:      return FText::FromString(TEXT("左移"));
	case EToyScreenAction::MoveRight:     return FText::FromString(TEXT("右移"));
	default:                              return FText();
	}
}
