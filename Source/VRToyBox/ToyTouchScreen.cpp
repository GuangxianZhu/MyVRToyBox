#include "ToyTouchScreen.h"

#include "ToyShapeComponent.h"
#include "ToyStudio.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	const FLinearColor StandColor(0.03f, 0.03f, 0.035f);
	const FLinearColor DisplayColor(0.02f, 0.05f, 0.12f);
	// 按钮面的中心在屏幕坐标 X = FaceX 处，按下时往里缩 0.5cm
	constexpr float FaceX = 1.8f;
	// 屏幕 32 × 22 cm。宽按钮两个一排，窄按钮四个一排（指尖大约 1.5cm 宽，7cm 的按钮在 VR 里也好戳）
	constexpr float WideWidth = 14.f;
	constexpr float NarrowWidth = 7.f;
	constexpr float ButtonHeight = 3.6f;
	constexpr float LineOffset = 0.8f;    // 按钮上两行字离按钮中心的高度
	constexpr float WideText = 1.5f;
	constexpr float NarrowText = 1.15f;
	// 窄按钮的四列、宽按钮的两列（屏幕坐标 Y，站在屏幕前看 +Y 在左手边）
	constexpr float Col4[4] = { 11.4f, 3.8f, -3.8f, -11.4f };
	constexpr float Col2[2] = { 7.6f, -7.6f };
	// 四排按钮的高度（标题在 9、状态栏在 -9.8）
	constexpr float Row[4] = { 5.5f, 1.6f, -2.3f, -6.2f };
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

	UToyShapeComponent::Create(this, TEXT("Bezel"), Pivot, EToyShape::Box, FVector(2.f, 34.f, 24.f),
		FVector::ZeroVector, StandColor, FRotator::ZeroRotator);
	UToyShapeComponent::Create(this, TEXT("Display"), Pivot, EToyShape::Box, FVector(0.4f, 32.f, 22.f),
		FVector(1.2f, 0.f, 0.f), DisplayColor, FRotator::ZeroRotator);

	// TextRender 自带字体没有中文字形，所以屏幕上用英文
	MakeText(TEXT("Title"), FVector(1.5f, 0.f, 9.f), TEXT("TOY BOX"), 2.f, FColor::White);
	Status = MakeText(TEXT("Status"), FVector(1.5f, 0.f, -9.8f), TEXT(""), 1.0f, FColor(150, 200, 255));

	// 第一排：方块（宽）。第二排：四个模型（窄）。第三排：爆炸 / 复位（宽）。
	// 第四排：挪模型（窄，灰蓝色），左 / 拉近 / 推远 / 右
	MakeButton(TEXT("Spawn"), EToyScreenAction::SpawnCube, Col2[0], Row[0], TEXT("SPAWN"), TEXT("CUBE"), FLinearColor(0.1f, 0.7f, 0.25f), WideWidth, WideText);
	MakeButton(TEXT("Clear"), EToyScreenAction::ClearCubes, Col2[1], Row[0], TEXT("CLEAR"), TEXT("CUBES"), FLinearColor(0.8f, 0.15f, 0.12f), WideWidth, WideText);
	MakeButton(TEXT("Motor"), EToyScreenAction::ToggleMotor, Col4[0], Row[1], TEXT("SHOW"), TEXT("MOTOR"), FLinearColor(0.15f, 0.35f, 0.85f), NarrowWidth, NarrowText);
	MakeButton(TEXT("Valve"), EToyScreenAction::ToggleValve, Col4[1], Row[1], TEXT("SHOW"), TEXT("VALVE"), FLinearColor(0.1f, 0.55f, 0.6f), NarrowWidth, NarrowText);
	MakeButton(TEXT("Drone"), EToyScreenAction::ToggleDrone, Col4[2], Row[1], TEXT("SHOW"), TEXT("DRONE"), FLinearColor(0.45f, 0.3f, 0.75f), NarrowWidth, NarrowText);
	MakeButton(TEXT("Lathe"), EToyScreenAction::ToggleLathe, Col4[3], Row[1], TEXT("SHOW"), TEXT("LATHE"), FLinearColor(0.7f, 0.25f, 0.45f), NarrowWidth, NarrowText);
	MakeButton(TEXT("Explode"), EToyScreenAction::ToggleExplode, Col2[0], Row[2], TEXT("EXPLODE"), TEXT("VIEW"), FLinearColor(0.85f, 0.5f, 0.05f), WideWidth, WideText);
	MakeButton(TEXT("Reset"), EToyScreenAction::ResetParts, Col2[1], Row[2], TEXT("RESET"), TEXT("PARTS"), FLinearColor(0.45f, 0.45f, 0.5f), WideWidth, WideText);
	const FLinearColor MoveColor(0.2f, 0.32f, 0.45f);
	MakeButton(TEXT("MoveLeft"), EToyScreenAction::MoveLeft, Col4[0], Row[3], TEXT("<<"), TEXT("LEFT"), MoveColor, NarrowWidth, NarrowText);
	MakeButton(TEXT("MoveNear"), EToyScreenAction::MoveNear, Col4[1], Row[3], TEXT("MOVE"), TEXT("NEAR"), MoveColor, NarrowWidth, NarrowText);
	MakeButton(TEXT("MoveFar"), EToyScreenAction::MoveFar, Col4[2], Row[3], TEXT("MOVE"), TEXT("FAR"), MoveColor, NarrowWidth, NarrowText);
	MakeButton(TEXT("MoveRight"), EToyScreenAction::MoveRight, Col4[3], Row[3], TEXT(">>"), TEXT("RIGHT"), MoveColor, NarrowWidth, NarrowText);
}

UTextRenderComponent* AToyTouchScreen::MakeText(FName Name, const FVector& Location, const FString& Text, float Size, const FColor& Color)
{
	UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(Name);
	Label->SetupAttachment(Pivot);
	Label->SetRelativeLocation(Location);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(Size);
	Label->SetTextRenderColor(Color);
	Label->SetText(FText::FromString(Text));
	Label->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	return Label;
}

void AToyTouchScreen::MakeButton(const FString& Id, EToyScreenAction Action, float Y, float Z, const FString& Line1,
	const FString& Line2, const FLinearColor& Color, float Width, float TextSize)
{
	FToyScreenButton& Button = Buttons.AddDefaulted_GetRef();
	Button.Action = Action;
	// 按钮面压暗到 45%：灯就在桌子正上方，原来的颜色在画面里接近粉色，白字几乎看不清
	Button.Color = FLinearColor(Color.R * 0.45f, Color.G * 0.45f, Color.B * 0.45f);
	Button.RestLocation = FVector(FaceX, Y, Z);
	Button.Face = UToyShapeComponent::Create(this, *(Id + TEXT("Face")), Pivot, EToyShape::Box,
		FVector(0.8f, Width, ButtonHeight), Button.RestLocation, Button.Color, FRotator::ZeroRotator);
	Button.Line1 = MakeText(*(Id + TEXT("Line1")), FVector(FaceX + 0.5f, Y, Z + LineOffset), Line1, TextSize, FColor::White);
	Button.Line2 = MakeText(*(Id + TEXT("Line2")), FVector(FaceX + 0.5f, Y, Z - LineOffset), Line2, TextSize, FColor::White);

	// 触发盒从按钮面里 0.8cm 一直延伸到面前 1.6cm：指尖快碰到就算按下，戳穿了也还在盒子里
	UBoxComponent* Zone = CreateDefaultSubobject<UBoxComponent>(*(Id + TEXT("Zone")));
	Zone->SetupAttachment(Pivot);
	Zone->SetRelativeLocation(FVector(FaceX + 0.8f, Y, Z));
	Zone->InitBoxExtent(FVector(1.2f, Width * 0.5f, ButtonHeight * 0.5f));
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionObjectType(ECC_WorldDynamic);
	Zone->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 桌面模式用 Visibility 射线点按钮
	Zone->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Button.Zone = Zone;
}

FToyScreenButton* AToyTouchScreen::FindButton(EToyScreenAction Action)
{
	return Buttons.FindByPredicate([Action](const FToyScreenButton& Button) { return Button.Action == Action; });
}

void AToyTouchScreen::SetStudio(AToyStudio* InStudio)
{
	Studio = InStudio;
	RefreshStatus();
}

bool AToyTouchScreen::Poke(UPrimitiveComponent* Touched)
{
	const int32 Index = Buttons.IndexOfByPredicate([Touched](const FToyScreenButton& Button) { return Button.Zone.Get() == Touched; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPressTime < PressCooldown)
	{
		return false;
	}
	LastPressTime = Now;

	Flash(Index);
	if (AToyStudio* StudioActor = Studio.Get())
	{
		StudioActor->HandleScreenAction(Buttons[Index].Action);
	}
	RefreshStatus();
	return true;
}

void AToyTouchScreen::RefreshStatus()
{
	const AToyStudio* StudioActor = Studio.Get();
	if (!StudioActor)
	{
		return;
	}
	Status->SetText(FText::FromString(StudioActor->GetStatusText()));

	// 按钮上的字跟着状态变：显示/隐藏电机、爆炸/还原
	if (FToyScreenButton* MotorButton = FindButton(EToyScreenAction::ToggleMotor))
	{
		MotorButton->Line1->SetText(FText::FromString(StudioActor->IsModelShown(EToyModelKind::Motor) ? TEXT("HIDE") : TEXT("SHOW")));
	}
	if (FToyScreenButton* ValveButton = FindButton(EToyScreenAction::ToggleValve))
	{
		ValveButton->Line1->SetText(FText::FromString(StudioActor->IsModelShown(EToyModelKind::Valve) ? TEXT("HIDE") : TEXT("SHOW")));
	}
	if (FToyScreenButton* DroneButton = FindButton(EToyScreenAction::ToggleDrone))
	{
		DroneButton->Line1->SetText(FText::FromString(StudioActor->IsModelShown(EToyModelKind::Drone) ? TEXT("HIDE") : TEXT("SHOW")));
	}
	if (FToyScreenButton* LatheButton = FindButton(EToyScreenAction::ToggleLathe))
	{
		LatheButton->Line1->SetText(FText::FromString(StudioActor->IsModelShown(EToyModelKind::Lathe) ? TEXT("HIDE") : TEXT("SHOW")));
	}
	if (FToyScreenButton* ExplodeButton = FindButton(EToyScreenAction::ToggleExplode))
	{
		ExplodeButton->Line1->SetText(FText::FromString(StudioActor->IsModelExploded() ? TEXT("ASSEMBLE") : TEXT("EXPLODE")));
	}
}

void AToyTouchScreen::Flash(int32 ButtonIndex)
{
	// 按下反馈：变白 + 往里缩一点，0.15 秒后恢复（冷却 0.35 秒，所以不会叠加）
	const FToyScreenButton& Button = Buttons[ButtonIndex];
	Button.Face->SetColor(FLinearColor::White);
	Button.Face->SetRelativeLocation(Button.RestLocation - FVector(0.5f, 0.f, 0.f));

	GetWorldTimerManager().SetTimer(FlashTimer, FTimerDelegate::CreateWeakLambda(this, [this, ButtonIndex]()
	{
		const FToyScreenButton& Pressed = Buttons[ButtonIndex];
		Pressed.Face->SetColor(Pressed.Color);
		Pressed.Face->SetRelativeLocation(Pressed.RestLocation);
	}), 0.15f, false);
}
