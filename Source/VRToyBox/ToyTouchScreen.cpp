#include "ToyTouchScreen.h"

#include "ToyEtchPanels.h"
#include "ToyEtcherRig.h"
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
	const FLinearColor ScreenDisplayColor(0.02f, 0.05f, 0.12f);

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

	/** 模型卡片（一页 7 个，多了翻页）：模型、按钮动作、中文名、英文代号、导入脚本名（scripts/import_*.bat）、颜色。 */
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
		{ EToyModelKind::Engine, EToyScreenAction::ToggleEngine, TEXT("四缸发动机"), TEXT("ENGINE"), TEXT("engine"), FLinearColor(0.8f, 0.15f, 0.12f) },
		{ EToyModelKind::DexHand, EToyScreenAction::ToggleDexHand, TEXT("五指灵巧手"), TEXT("HAND-20"), TEXT("hand"), FLinearColor(0.95f, 0.5f, 0.1f) },
		{ EToyModelKind::Etcher, EToyScreenAction::ToggleEtcher, TEXT("干法刻蚀机"), TEXT("ETCHER"), TEXT("etcher"), FLinearColor(0.55f, 0.35f, 0.95f) },
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

	/** 把触屏（或刻蚀机演示）的一个 const 成员函数包成 Slate 每帧读值的 lambda；对象没了就返回 Fallback。 */
	template <typename TObject, typename R, typename... TParams, typename... TArgs>
	auto Live(TWeakObjectPtr<TObject> Weak, R (TObject::*Getter)(TParams...) const, R Fallback, TArgs... Args)
	{
		return [Weak, Getter, Fallback, Args...]() -> R
		{
			const TObject* Object = Weak.Get();
			return Object ? (Object->*Getter)(Args...) : Fallback;
		};
	}

	// ---------- 刻蚀机页签 ----------
	// 页签栏 96~150 占掉原来"模型"小标题那一行；页签下面 160~860 是页签的内容，右边那块（当前模型、爆炸、挪动）不变
	const TCHAR* const EtchTabNames[] = { TEXT("模型"), TEXT("结构"), TEXT("流程"), TEXT("配方"), TEXT("结果") };
	constexpr int32 EtchTabCount = UE_ARRAY_COUNT(EtchTabNames);
	constexpr int32 EtchTabModels = 0;
	constexpr int32 EtchTabStructure = 1;
	constexpr int32 EtchTabFlow = 2;
	constexpr int32 EtchTabRecipe = 3;
	constexpr int32 EtchTabResult = 4;
	/** "桌上不是刻蚀机时才显示"（原来的"模型"小标题），给 OnTab 用 */
	constexpr int32 NoEtcherTab = -2;
	// 配方页：8 个参数、5 个预置（多了不显示）
	constexpr int32 EtchFieldRows = 8;
	constexpr int32 EtchPresetSlots = 5;
	const FLinearColor EtchGo(0.04f, 0.25f, 0.1f);
	const FLinearColor EtchPresetColor(0.14f, 0.1f, 0.26f);
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
		FVector(1.2f, 0.f, 0.f), ScreenDisplayColor, FRotator::ZeroRotator);

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
	// 桌上摆着刻蚀机时，左边 96~150 是页签栏，上面这些模型卡片、方块按钮只在"模型"页签里，其余页签见下面
	Items.Reset();
	auto Add = [this](EToyScreenAction Action, const FBox2D& Rect, const FLinearColor& Color, int32 InTab = INDEX_NONE, int32 InParam = 0) -> FToyScreenItem&
	{
		FToyScreenItem& Item = Items.AddDefaulted_GetRef();
		Item.Action = Action;
		Item.Rect = Rect;
		Item.Color = Color;
		Item.Tab = InTab;
		Item.Param = InParam;
		return Item;
	};

	Add(EToyScreenAction::OpenMenu, PixelRect(1090.f, 18.f, 170.f, 60.f), GreyButton);

	for (int32 Index = 0; Index < ModelCount; ++Index)
	{
		FToyScreenItem& Card = Add(ModelTable[Index].Action, CardRect(Index % ModelsPerPage), Scaled(ModelTable[Index].Color, 0.22f), EtchTabModels);
		Card.Page = Index / ModelsPerPage;
		Card.Model = Index;
	}
	Add(EToyScreenAction::HideModel, CardRect(ModelsPerPage), GreyButton, EtchTabModels);
	// 翻页箭头：模型超过 7 个才出现（IsItemVisible）
	Add(EToyScreenAction::PrevPage, PixelRect(690.f, 96.f, 74.f, 46.f), GreyButton, EtchTabModels);
	Add(EToyScreenAction::NextPage, PixelRect(776.f, 96.f, 74.f, 46.f), GreyButton, EtchTabModels);

	Add(EToyScreenAction::SpawnCube, PixelRect(30.f, 700.f, 400.f, 140.f), FLinearColor(0.04f, 0.25f, 0.1f), EtchTabModels);
	Add(EToyScreenAction::ClearCubes, PixelRect(450.f, 700.f, 400.f, 140.f), FLinearColor(0.3f, 0.06f, 0.05f), EtchTabModels);

	Add(EToyScreenAction::ToggleExplode, PixelRect(890.f, 236.f, 350.f, 120.f), FLinearColor(0.34f, 0.2f, 0.02f));
	Add(EToyScreenAction::ResetParts, PixelRect(890.f, 372.f, 350.f, 96.f), GreyButton);

	// 方向键 110 × 100（≈ 2.8 × 2.5cm），中间一格显示距离
	Add(EToyScreenAction::MoveFar, PixelRect(1010.f, 530.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveLeft, PixelRect(890.f, 640.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveRight, PixelRect(1130.f, 640.f, 110.f, 100.f), MoveButton);
	Add(EToyScreenAction::MoveNear, PixelRect(1010.f, 750.f, 110.f, 100.f), MoveButton);

	// ---------- 刻蚀机页签（120 × 54，≈ 3 × 1.4cm）----------
	for (int32 Tab = 0; Tab < EtchTabCount; ++Tab)
	{
		Add(EToyScreenAction::EtchTab, PixelRect(30.f + Tab * 130.f, 96.f, 120.f, 54.f), GreyButton, INDEX_NONE, Tab);
	}
	// 结构：放大腔和面板的显示开关（爆炸 / 复位就是右边那两个）
	Add(EToyScreenAction::EtchBoards, PixelRect(50.f, 740.f, 520.f, 90.f), GreyButton, EtchTabStructure);
	// 流程：左边 15 步的列表（不是按钮），右边开始 / 下一步、自动 / 暂停 / 复位、倍速，下面平台小地图和状态
	Add(EToyScreenAction::EtchNext, PixelRect(430.f, 160.f, 420.f, 120.f), EtchGo, EtchTabFlow);
	Add(EToyScreenAction::EtchAuto, PixelRect(430.f, 296.f, 130.f, 80.f), GreyButton, EtchTabFlow);
	Add(EToyScreenAction::EtchPause, PixelRect(575.f, 296.f, 130.f, 80.f), GreyButton, EtchTabFlow);
	Add(EToyScreenAction::EtchReset, PixelRect(720.f, 296.f, 130.f, 80.f), GreyButton, EtchTabFlow);
	for (int32 Speed = 0; Speed < AToyEtcherRig::SpeedCount; ++Speed)
	{
		Add(EToyScreenAction::EtchSpeed, PixelRect(430.f + Speed * 105.f, 392.f, 95.f, 70.f), MoveButton, EtchTabFlow, Speed);
	}
	// 配方：一排预置，下面 8 个参数各一行 [-] 数值 [+]，右边是"按这个配方预计"
	for (int32 Preset = 0; Preset < EtchPresetSlots; ++Preset)
	{
		Add(EToyScreenAction::EtchPreset, PixelRect(30.f + Preset * 166.f, 160.f, 152.f, 70.f), EtchPresetColor, EtchTabRecipe, Preset);
	}
	for (int32 Field = 0; Field < EtchFieldRows; ++Field)
	{
		const float Y = 246.f + Field * 64.f;
		Add(EToyScreenAction::EtchFieldDown, PixelRect(236.f, Y, 70.f, 56.f), MoveButton, EtchTabRecipe, Field);
		Add(EToyScreenAction::EtchFieldUp, PixelRect(444.f, Y, 70.f, 56.f), MoveButton, EtchTabRecipe, Field);
	}
	// 结果：再跑一片、改配方（网页版结果弹窗底下那两个）
	Add(EToyScreenAction::EtchAgain, PixelRect(30.f, 776.f, 400.f, 76.f), EtchGo, EtchTabResult);
	Add(EToyScreenAction::EtchTweak, PixelRect(450.f, 776.f, 400.f, 76.f), GreyButton, EtchTabResult);
}

TSharedRef<SWidget> AToyTouchScreen::BuildGui()
{
	const TWeakObjectPtr<AToyTouchScreen> Weak(this);
	const FToyScreenBrushes& B = *Brushes;
	TSharedRef<SCanvas> Canvas = SNew(SCanvas);

	// 只在某个页签显示：INDEX_NONE = 一直显示，NoEtcherTab = 桌上不是刻蚀机时才显示
	auto OnTab = [Weak](int32 Tab) -> TAttribute<EVisibility>
	{
		if (Tab == INDEX_NONE)
		{
			return EVisibility::HitTestInvisible;
		}
		return TAttribute<EVisibility>::CreateLambda([Weak, Tab]()
		{
			const AToyTouchScreen* Screen = Weak.Get();
			const bool bShow = Screen && (Tab == NoEtcherTab ? Screen->GetRig() == nullptr : Screen->CurrentTab() == Tab);
			return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		});
	};
	auto AddBox = [&Canvas, &OnTab](const FBox2D& Rect, const FSlateBrush* Brush, const FLinearColor& Color, int32 Tab = INDEX_NONE)
	{
		Canvas->AddSlot().Position(Rect.Min).Size(Rect.GetSize())
		[
			SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(Color).Visibility(OnTab(Tab))
		];
	};
	// SCanvas 槽的 HAlign / VAlign 是"控件相对 Position 这个点怎么对齐"（Center = 以 Position 为中心），不是在 Size 里居中。
	// 槽一律左上对齐，在 Rect 里的对齐交给里面的 SBox
	auto AddWidget = [&Canvas, &OnTab](const FBox2D& Rect, EHorizontalAlignment HAlign, const TSharedRef<SWidget>& Widget, int32 Tab = INDEX_NONE,
		EVerticalAlignment VAlign = VAlign_Center)
	{
		Canvas->AddSlot().Position(Rect.Min).Size(Rect.GetSize())
		[
			SNew(SBox).HAlign(HAlign).VAlign(VAlign).Visibility(OnTab(Tab))
			[
				Widget
			]
		];
	};
	auto AddText = [&AddWidget](const FBox2D& Rect, TAttribute<FText> Text, int32 Size, const FLinearColor& Color, EHorizontalAlignment HAlign, int32 Tab = INDEX_NONE)
	{
		AddWidget(Rect, HAlign, SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color), Tab);
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

	// 左边的小标题（桌上摆着刻蚀机时这一行是页签栏）
	AddText(PixelRect(30.f, 96.f, 300.f, 46.f), FText::FromString(TEXT("模型")), 20, DimTextColor, HAlign_Left, NoEtcherTab);
	AddText(PixelRect(30.f, 646.f, 820.f, 46.f), TAttribute<FText>::CreateLambda(Live(Weak, &AToyTouchScreen::CubeText, FText())), 20, DimTextColor, HAlign_Left, EtchTabModels);

	// 右边：当前模型的名字和状态、挪动标题、方向键中间的距离
	AddText(PixelRect(890.f, 104.f, 350.f, 34.f), FText::FromString(TEXT("当前模型")), 16, DimTextColor, HAlign_Left);
	AddText(PixelRect(890.f, 136.f, 350.f, 50.f), TAttribute<FText>::CreateLambda(Live(Weak, &AToyTouchScreen::ModelTitle, FText())), 28, TextColor, HAlign_Left);
	AddWidget(PixelRect(890.f, 186.f, 350.f, 40.f), HAlign_Left,
		SNew(STextBlock).Text_Lambda(Live(Weak, &AToyTouchScreen::ModelState, FText())).Font(Font(18, false)).ColorAndOpacity(DimTextColor));
	AddText(PixelRect(890.f, 482.f, 350.f, 40.f), FText::FromString(TEXT("挪动模型")), 18, DimTextColor, HAlign_Left);
	AddWidget(PixelRect(1010.f, 640.f, 110.f, 100.f), HAlign_Center,
		SNew(STextBlock).Text_Lambda(Live(Weak, &AToyTouchScreen::DistanceText, FText())).Font(Font(18)).ColorAndOpacity(DimTextColor)
		.Justification(ETextJustify::Center));

	// ---------- 刻蚀机页签的内容（不是按钮的部分）：字和图直接读刻蚀机演示，换了刻蚀机就整个界面重建（SetEtcherRig）----------
	const TWeakObjectPtr<AToyEtcherRig> Rig = EtchRig;
	if (Rig.IsValid())
	{
		const FLinearColor Title(1.f, 0.8f, 0.3f);
		const FLinearColor Good(0.6f, 0.95f, 0.65f);

		// 结构：三级放大怎么看
		AddBox(PixelRect(30.f, 160.f, 820.f, 700.f), &B.Panel, PanelColor, EtchTabStructure);
		AddText(PixelRect(50.f, 176.f, 780.f, 50.f), FText::FromString(TEXT("三级放大：从整机看到一根栅极")), 26, Title, HAlign_Left, EtchTabStructure);
		AddWidget(PixelRect(50.f, 236.f, 780.f, 490.f), HAlign_Left,
			SNew(STextBlock)
			.Text(FText::FromString(
				TEXT("1. 桌上的整机（真实的三分之一左右）：跑工艺时闸阀门落下、升降针顶起，真空机械手的手指托着晶圆从闸阀伸进伸出；起辉以后腔里的紫光透过腔壁也看得见。\n\n")
				TEXT("2. 左边放大的反应腔（真实的 60%）：气体分子从喷嘴进来，被电子撞碎成离子和自由基；自由基吸附在晶圆上，离子被鞘层电场加速打下来，生成 SiCl4 冒出、随气流被抽走。\n\n")
				TEXT("3. 上面的剖面板：晶圆上一根 60 nm 宽的栅极，刻深、侧壁钻蚀、栅氧、光刻胶都实时画出来（放大一百万倍）。\n\n")
				TEXT("想看零件：右边「爆炸拆开」，零件可以拿起来看名字和作用。一片正在跑的时候不能拆，拆开的时候也不能开始。")))
			.Font(Font(19, false))
			.ColorAndOpacity(TextColor)
			.WrapTextAt(780.f)
			.LineHeightPercentage(1.15f),
			EtchTabStructure, VAlign_Top);

		// 流程：15 步的列表，做完的打勾、正在做的高亮
		for (int32 Step = 0; Step < ToyEtch::StepCount; ++Step)
		{
			Canvas->AddSlot().Position(FVector2D(30.f, 166.f + Step * 44.f)).Size(FVector2D(380.f, 40.f))
			[
				SNew(SBorder)
				.BorderImage(&B.Swatch)
				.Visibility(OnTab(EtchTabFlow))
				.Padding(FMargin(14.f, 0.f))
				.VAlign(VAlign_Center)
				.BorderBackgroundColor_Lambda([Rig, Step]()
				{
					const int32 State = Rig.IsValid() ? Rig->StepState(Step) : 0;
					return FSlateColor(State == 1 ? FLinearColor(0.34f, 0.2f, 0.02f) : State == 2 ? FLinearColor(0.03f, 0.1f, 0.05f) : FLinearColor(0.045f, 0.055f, 0.085f));
				})
				[
					SNew(STextBlock)
					.Font(Font(18, false))
					.Text_Lambda([Rig, Step]()
					{
						const int32 State = Rig.IsValid() ? Rig->StepState(Step) : 0;
						const TCHAR* Mark = State == 2 ? TEXT("√") : State == 1 ? TEXT("●") : TEXT("○");
						const FString Name = Rig.IsValid() ? Rig->StepName(Step).ToString() : FString();
						return FText::FromString(FString::Printf(TEXT("%s  %d. %s"), Mark, Step + 1, *Name));
					})
					.ColorAndOpacity_Lambda([Rig, Step, Good]()
					{
						const int32 State = Rig.IsValid() ? Rig->StepState(Step) : 0;
						return FSlateColor(State == 1 ? TextColor : State == 2 ? Good : DimTextColor);
					})
				]
			];
		}
		AddBox(PixelRect(430.f, 478.f, 420.f, 270.f), &B.Swatch, PanelColor, EtchTabFlow);
		Canvas->AddSlot().Position(FVector2D(436.f, 484.f)).Size(FVector2D(408.f, 258.f))
		[
			SNew(SBox).Visibility(OnTab(EtchTabFlow))
			[
				SNew(SToyEtchMap).Rig(Rig)
			]
		];
		AddWidget(PixelRect(430.f, 756.f, 420.f, 90.f), HAlign_Left,
			SNew(STextBlock)
			.Text_Lambda([Rig]()
			{
				return Rig.IsValid() ? FText::FromString(FString::Printf(TEXT("%s\n%s  %s"), *Rig->StatusLine().ToString(),
					*Rig->DepthText().ToString(), *Rig->EndpointText().ToString())) : FText();
			})
			.Font(Font(17, false))
			.ColorAndOpacity(DimTextColor)
			.WrapTextAt(420.f),
			EtchTabFlow);

		// 配方：参数名 / 数值（两边的 - + 是按钮）、预计、提示
		for (int32 Field = 0; Field < EtchFieldRows; ++Field)
		{
			const float Y = 246.f + Field * 64.f;
			AddText(PixelRect(30.f, Y, 200.f, 56.f), TAttribute<FText>::CreateLambda(Live(Rig, &AToyEtcherRig::FieldName, FText(), Field)), 19, TextColor, HAlign_Left, EtchTabRecipe);
			AddText(PixelRect(310.f, Y, 130.f, 56.f), TAttribute<FText>::CreateLambda(Live(Rig, &AToyEtcherRig::FieldValue, FText(), Field)), 22, TextColor, HAlign_Center, EtchTabRecipe);
		}
		AddBox(PixelRect(530.f, 246.f, 320.f, 504.f), &B.Panel, PanelColor, EtchTabRecipe);
		AddWidget(PixelRect(550.f, 262.f, 290.f, 480.f), HAlign_Left,
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("按这个配方，预计："))).Font(Font(19)).ColorAndOpacity(Title)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text_Lambda(Live(Rig, &AToyEtcherRig::PredictText, FText())).Font(Font(16, false)).ColorAndOpacity(TextColor)
				.WrapTextAt(290.f).LineHeightPercentage(1.25f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("简化模型：趋势对、数量级对，具体数字是估计值。"))).Font(Font(14, false)).ColorAndOpacity(DimTextColor)
				.WrapTextAt(290.f)
			],
			EtchTabRecipe, VAlign_Top);
		AddWidget(PixelRect(30.f, 766.f, 820.f, 90.f), HAlign_Left,
			SNew(STextBlock)
			.Text_Lambda(Live(Rig, &AToyEtcherRig::FieldTip, FText()))
			.Font(Font(17, false))
			.ColorAndOpacity_Lambda([Rig]() { return FSlateColor(Rig.IsValid() && Rig->IsRunning() ? WarnColor : DimTextColor); })
			.WrapTextAt(820.f),
			EtchTabRecipe);

		// 结果：和右边浮着的结果板是同一份列表
		AddBox(PixelRect(30.f, 160.f, 820.f, 604.f), &B.Panel, PanelColor, EtchTabResult);
		Canvas->AddSlot().Position(FVector2D(50.f, 176.f)).Size(FVector2D(780.f, 576.f))
		[
			SNew(SBox).Visibility(OnTab(EtchTabResult)).VAlign(VAlign_Top)
			[
				ToyEtchUI::MakeResultList(Rig, 0.85f, &B.Swatch)
			]
		];
	}

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

	// 按钮的字：按钮里放不下就折行（刻蚀机的"下一步：吸附晶圆、通背氦"这种）
	return SNew(STextBlock)
		.Text_Lambda(Live(Weak, &AToyTouchScreen::ItemLabel, FText(), Index))
		.Font(Font(ItemFontSize(Index)))
		.Justification(ETextJustify::Center)
		.WrapTextAt(static_cast<float>(Item.Rect.GetSize().X) - 28.f)
		.ColorAndOpacity_Lambda(TextColorOf);
}

int32 AToyTouchScreen::ItemFontSize(int32 Index) const
{
	const FToyScreenItem& Item = Items[Index];
	switch (Item.Action)
	{
	case EToyScreenAction::EtchNext:      return 26;
	case EToyScreenAction::EtchSpeed:     return 24;
	case EToyScreenAction::EtchPreset:    return 18;
	case EToyScreenAction::EtchFieldDown:
	case EToyScreenAction::EtchFieldUp:   return 32;
	default:                              return Item.Rect.GetSize().Y >= 110.f ? 28 : 22;
	}
}

AToyEtcherRig* AToyTouchScreen::GetRig() const
{
	return EtchRig.Get();
}

int32 AToyTouchScreen::CurrentTab() const
{
	return EtchRig.IsValid() ? EtchTab : EtchTabModels;
}

void AToyTouchScreen::SetEtcherRig(AToyEtcherRig* Rig)
{
	EtchRig = Rig;
	EtchTab = Rig ? EtchTabFlow : EtchTabModels;
	// 刻蚀机页签里的字和图绑的是这一台刻蚀机演示，换了就重建界面（BeginPlay 之前还没有界面，不用管）
	if (Brushes.IsValid() && Gui)
	{
		Gui->SetSlateWidget(BuildGui());
	}
}

void AToyTouchScreen::SetEtchTab(int32 Tab)
{
	EtchTab = FMath::Clamp(Tab, 0, EtchTabCount - 1);
}

int32 AToyTouchScreen::PageCount() const
{
	return FMath::Max(1, FMath::DivideAndRoundUp(ModelCount, ModelsPerPage));
}

bool AToyTouchScreen::IsItemVisible(int32 Index) const
{
	const FToyScreenItem& Item = Items[Index];
	if (Item.Tab != INDEX_NONE && Item.Tab != CurrentTab())
	{
		return false;
	}
	const AToyEtcherRig* Rig = GetRig();
	switch (Item.Action)
	{
	case EToyScreenAction::PrevPage:
	case EToyScreenAction::NextPage:
		return PageCount() > 1;
	case EToyScreenAction::EtchTab:
		return Rig != nullptr;
	case EToyScreenAction::EtchPreset:
		return Rig && Rig->GetContent().Presets.IsValidIndex(Item.Param);
	case EToyScreenAction::EtchFieldDown:
	case EToyScreenAction::EtchFieldUp:
		return Rig && Rig->GetContent().Fields.IsValidIndex(Item.Param);
	default:
		return Item.Page == INDEX_NONE || Item.Page == Page;
	}
}

bool AToyTouchScreen::IsItemEnabled(int32 Index) const
{
	const AToyStudio* StudioActor = Studio.Get();
	const AToyEtcherRig* Rig = GetRig();
	switch (Items[Index].Action)
	{
	case EToyScreenAction::HideModel:
	case EToyScreenAction::ResetParts:
		return StudioActor && StudioActor->HasModel();
	case EToyScreenAction::PrevPage:
		return Page > 0;
	case EToyScreenAction::NextPage:
		return Page < PageCount() - 1;
	// 刻蚀机：一片正在跑的时候不能拆开、不能改配方
	case EToyScreenAction::ToggleExplode:
		return !(Rig && Rig->IsRunning());
	case EToyScreenAction::EtchNext:
		return Rig && Rig->CanStart();
	case EToyScreenAction::EtchReset:
		return Rig && !Rig->GetProcess().bBusy && Rig->GetProcess().StepIdx >= 0;
	case EToyScreenAction::EtchPreset:
	case EToyScreenAction::EtchFieldDown:
	case EToyScreenAction::EtchFieldUp:
	case EToyScreenAction::EtchAgain:
		return Rig && !Rig->IsRunning();
	case EToyScreenAction::EtchTab:
	case EToyScreenAction::EtchAuto:
	case EToyScreenAction::EtchPause:
	case EToyScreenAction::EtchSpeed:
	case EToyScreenAction::EtchTweak:
	case EToyScreenAction::EtchBoards:
		return Rig != nullptr;
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
	const FToyScreenItem& Item = Items[Index];
	const AToyEtcherRig* Rig = GetRig();
	switch (Item.Action)
	{
	case EToyScreenAction::EtchTab:    return Rig && CurrentTab() == Item.Param;
	case EToyScreenAction::EtchSpeed:  return Rig && Rig->GetSpeedIndex() == Item.Param;
	case EToyScreenAction::EtchPreset: return Rig && Rig->IsPresetSelected(Item.Param);
	case EToyScreenAction::EtchAuto:   return Rig && Rig->IsAuto();
	case EToyScreenAction::EtchPause:  return Rig && Rig->IsPaused();
	default: break;
	}
	const AToyStudio* StudioActor = Studio.Get();
	return StudioActor && Item.Model != INDEX_NONE && StudioActor->IsModelShown(ModelTable[Item.Model].Kind);
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
	const int32 Param = Items[Index].Param;
	if (Action == EToyScreenAction::PrevPage || Action == EToyScreenAction::NextPage)
	{
		Page = FMath::Clamp(Page + (Action == EToyScreenAction::NextPage ? 1 : -1), 0, PageCount() - 1);
	}
	else if (Action == EToyScreenAction::EtchTab)
	{
		SetEtchTab(Param);
	}
	else if (Action == EToyScreenAction::EtchTweak)
	{
		SetEtchTab(EtchTabRecipe);
	}
	else if (AToyStudio* StudioActor = Studio.Get())
	{
		StudioActor->HandleScreenAction(Action, Param);
		// 再跑一片：回到待机以后切到"流程"，接着点开始
		if (Action == EToyScreenAction::EtchAgain)
		{
			SetEtchTab(EtchTabFlow);
		}
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
	const AToyEtcherRig* Rig = GetRig();
	if (Rig && Rig->IsRunning())
	{
		return FText::FromString(TEXT("工艺进行中 · 跑完才能拆开"));
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
	default: break;
	}

	// 刻蚀机页签里的按钮
	const AToyEtcherRig* Rig = GetRig();
	if (!Rig)
	{
		return FText();
	}
	switch (Item.Action)
	{
	case EToyScreenAction::EtchTab:       return FText::FromString(EtchTabNames[FMath::Clamp(Item.Param, 0, EtchTabCount - 1)]);
	case EToyScreenAction::EtchNext:      return Rig->StartLabel();
	case EToyScreenAction::EtchAuto:      return FText::FromString(Rig->IsAuto() ? TEXT("自动：开") : TEXT("自动：关"));
	case EToyScreenAction::EtchPause:     return FText::FromString(Rig->IsPaused() ? TEXT("继续") : TEXT("暂停"));
	case EToyScreenAction::EtchReset:     return FText::FromString(TEXT("复位"));
	case EToyScreenAction::EtchSpeed:     return FText::FromString(FString::Printf(TEXT("%g×"), AToyEtcherRig::SpeedValue(Item.Param)));
	case EToyScreenAction::EtchPreset:    return Rig->PresetName(Item.Param);
	case EToyScreenAction::EtchFieldDown: return FText::FromString(TEXT("-"));
	case EToyScreenAction::EtchFieldUp:   return FText::FromString(TEXT("+"));
	case EToyScreenAction::EtchAgain:     return FText::FromString(TEXT("再跑一片"));
	case EToyScreenAction::EtchTweak:     return FText::FromString(TEXT("改配方再试"));
	case EToyScreenAction::EtchBoards:    return FText::FromString(Rig->AreBoardsShown() ? TEXT("隐藏放大腔和面板") : TEXT("显示放大腔和面板"));
	default:                              return FText();
	}
}
