#include "ToyMenuPanel.h"

#include "ToyBoxPawn.h"
#include "ToyFeedback.h"
#include "ToyLilliput.h"
#include "ToySettings.h"
#include "ToyShapeComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

namespace
{
	// 颜色和触屏一致：黑色边框、深蓝屏幕
	const FLinearColor BezelColor(0.03f, 0.03f, 0.035f);
	const FLinearColor DisplayColor(0.02f, 0.05f, 0.12f);
	// 面板 40 × 28 cm，比触屏大一圈：飘在空中、离眼睛近，字要看得清
	constexpr float PanelWidth = 40.f;
	constexpr float PanelHeight = 28.f;
	constexpr float FaceX = 1.8f;
	// 往后仰 35°：面板在眼睛前 45cm、低 35cm，视线往下约 38°，仰 35° 差不多正对着眼睛
	constexpr float PanelPitch = 35.f;

	// 设置页：两列五排，每个按钮按一次换一档
	constexpr float SetCol[2] = { 9.2f, -9.2f };
	constexpr float SetRow[5] = { 7.f, 3.f, -1.f, -5.f, -9.f };
	constexpr float SetWidth = 17.f;
	constexpr float SetHeight = 3.4f;

	// 调试命令 toy.Menu <动作>：open / close / start / settings / back / turn / speed / teleport / ball / grab / res / calib
	void MenuCommand(const TArray<FString>& Args, UWorld* World)
	{
		static const TMap<FString, EToyMenuAction> Names = {
			{ TEXT("start"), EToyMenuAction::Start }, { TEXT("settings"), EToyMenuAction::OpenSettings },
			{ TEXT("back"), EToyMenuAction::Back }, { TEXT("turn"), EToyMenuAction::Turn },
			{ TEXT("speed"), EToyMenuAction::MoveSpeed }, { TEXT("teleport"), EToyMenuAction::Teleport },
			{ TEXT("ball"), EToyMenuAction::PinchBall }, { TEXT("grab"), EToyMenuAction::GrabSize },
			{ TEXT("res"), EToyMenuAction::Resolution }, { TEXT("calib"), EToyMenuAction::CalibrateHands },
			{ TEXT("sound"), EToyMenuAction::Sound }, { TEXT("haptics"), EToyMenuAction::Haptics },
		};
		AToyMenuPanel* Menu = nullptr;
		for (TActorIterator<AToyMenuPanel> It(World); It && !Menu; ++It)
		{
			Menu = *It;
		}
		if (!Menu)
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Menu 找不到菜单面板"));
			return;
		}
		for (const FString& Arg : Args)
		{
			const FString Name = Arg.ToLower();
			if (Name == TEXT("open"))
			{
				Menu->Open();
			}
			else if (Name == TEXT("close"))
			{
				Menu->Close();
			}
			else if (const EToyMenuAction* Action = Names.Find(Name))
			{
				Menu->RunAction(*Action);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox: toy.Menu 不认识 %s"), *Arg);
				continue;
			}
			UE_LOG(LogTemp, Log, TEXT("ToyBox: toy.Menu %s → %s，%s"), *Arg, Menu->IsOpen() ? TEXT("菜单开着") : TEXT("菜单收起"),
				*ToySettings::Describe());
		}
	}

	FAutoConsoleCommandWithWorldAndArgs MenuCommandRef(TEXT("toy.Menu"),
		TEXT("和戳菜单按钮一样：toy.Menu open settings turn speed back start（可以连着写几个）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MenuCommand));
}

AToyMenuPanel::AToyMenuPanel()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Pivot->SetRelativeRotation(FRotator(PanelPitch, 0.f, 0.f));

	UToyShapeComponent* Bezel = UToyShapeComponent::Create(this, TEXT("Bezel"), Pivot, EToyShape::Box,
		FVector(2.f, PanelWidth + 2.f, PanelHeight + 2.f), FVector::ZeroVector, BezelColor, FRotator::ZeroRotator);
	UToyShapeComponent* Display = UToyShapeComponent::Create(this, TEXT("Display"), Pivot, EToyShape::Box,
		FVector(0.4f, PanelWidth, PanelHeight), FVector(1.2f, 0.f, 0.f), DisplayColor, FRotator::ZeroRotator);
	// 飘在空中的面板不挡路、不投影（不然桌上会多一块影子）；按钮的触发盒单独设碰撞
	for (UToyShapeComponent* Shape : { Bezel, Display })
	{
		Shape->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Shape->SetCastShadow(false);
	}

	Title = MakeText(TEXT("Title"), FVector(1.5f, 0.f, 10.8f), TEXT("TOY BOX"), 2.6f, FColor::White);
	Hint = MakeText(TEXT("Hint"), FVector(1.5f, 0.f, -12.3f), TEXT(""), 0.9f, FColor(150, 200, 255));

	// 主页：大大的 START，下面 SETTINGS，最下面小一点的 QUIT
	MakeButton(TEXT("Start"), EToyMenuPage::Main, EToyMenuAction::Start, 0.f, 3.5f, 24.f, 6.f,
		TEXT("START"), TEXT(""), FLinearColor(0.1f, 0.7f, 0.25f), 2.2f);
	MakeButton(TEXT("Settings"), EToyMenuPage::Main, EToyMenuAction::OpenSettings, 0.f, -3.6f, 24.f, 4.6f,
		TEXT("SETTINGS"), TEXT(""), FLinearColor(0.15f, 0.35f, 0.85f), 1.7f);
	MakeButton(TEXT("Quit"), EToyMenuPage::Main, EToyMenuAction::Quit, 0.f, -9.f, 12.f, 3.4f,
		TEXT("QUIT"), TEXT(""), FLinearColor(0.8f, 0.15f, 0.12f), 1.3f);

	// 设置页：第一行字是设置名，第二行是现在的档位（RefreshLabels 填）
	const FLinearColor Comfort(0.2f, 0.32f, 0.45f);
	const FLinearColor HandColor(0.45f, 0.3f, 0.75f);
	MakeButton(TEXT("Turn"), EToyMenuPage::Settings, EToyMenuAction::Turn, SetCol[0], SetRow[0], SetWidth, SetHeight,
		TEXT("TURN"), TEXT(""), Comfort, 1.25f);
	MakeButton(TEXT("Speed"), EToyMenuPage::Settings, EToyMenuAction::MoveSpeed, SetCol[1], SetRow[0], SetWidth, SetHeight,
		TEXT("MOVE SPEED"), TEXT(""), Comfort, 1.25f);
	MakeButton(TEXT("Teleport"), EToyMenuPage::Settings, EToyMenuAction::Teleport, SetCol[0], SetRow[1], SetWidth, SetHeight,
		TEXT("TELEPORT"), TEXT(""), Comfort, 1.25f);
	MakeButton(TEXT("Resolution"), EToyMenuPage::Settings, EToyMenuAction::Resolution, SetCol[1], SetRow[1], SetWidth, SetHeight,
		TEXT("RESOLUTION"), TEXT(""), FLinearColor(0.1f, 0.55f, 0.6f), 1.25f);
	MakeButton(TEXT("Ball"), EToyMenuPage::Settings, EToyMenuAction::PinchBall, SetCol[0], SetRow[2], SetWidth, SetHeight,
		TEXT("PINCH BALL"), TEXT(""), HandColor, 1.25f);
	MakeButton(TEXT("Grab"), EToyMenuPage::Settings, EToyMenuAction::GrabSize, SetCol[1], SetRow[2], SetWidth, SetHeight,
		TEXT("GRAB RANGE"), TEXT(""), HandColor, 1.25f);
	MakeButton(TEXT("Calib"), EToyMenuPage::Settings, EToyMenuAction::CalibrateHands, SetCol[0], SetRow[3], SetWidth, SetHeight,
		TEXT("CALIBRATE"), TEXT("HANDS"), FLinearColor(0.85f, 0.5f, 0.05f), 1.25f);
	// 声音、震动：设置名 + 关 / 弱 / 强（OFF / LOW / HIGH）
	const FLinearColor FeelColor(0.75f, 0.35f, 0.45f);
	MakeButton(TEXT("Sound"), EToyMenuPage::Settings, EToyMenuAction::Sound, SetCol[1], SetRow[3], SetWidth, SetHeight,
		TEXT("SOUND"), TEXT(""), FeelColor, 1.25f);
	MakeButton(TEXT("Haptics"), EToyMenuPage::Settings, EToyMenuAction::Haptics, SetCol[0], SetRow[4], SetWidth, SetHeight,
		TEXT("HAPTICS"), TEXT(""), FeelColor, 1.25f);
	MakeButton(TEXT("Back"), EToyMenuPage::Settings, EToyMenuAction::Back, SetCol[1], SetRow[4], SetWidth, SetHeight,
		TEXT("BACK"), TEXT(""), FLinearColor(0.45f, 0.45f, 0.5f), 1.5f);
}

UTextRenderComponent* AToyMenuPanel::MakeText(FName Name, const FVector& Location, const FString& Text, float Size, const FColor& Color)
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
	Label->SetCastShadow(false);
	return Label;
}

void AToyMenuPanel::MakeButton(const FString& Id, EToyMenuPage Page, EToyMenuAction Action, float Y, float Z, float Width,
	float Height, const FString& Line1, const FString& Line2, const FLinearColor& Color, float TextSize)
{
	FToyMenuButton& Button = Buttons.AddDefaulted_GetRef();
	Button.Action = Action;
	Button.Page = Page;
	// 和触屏一样压暗到 45%，白字才看得清
	Button.Color = FLinearColor(Color.R * 0.45f, Color.G * 0.45f, Color.B * 0.45f);
	Button.RestLocation = FVector(FaceX, Y, Z);
	Button.Face = UToyShapeComponent::Create(this, *(Id + TEXT("Face")), Pivot, EToyShape::Box,
		FVector(0.8f, Width, Height), Button.RestLocation, Button.Color, FRotator::ZeroRotator);
	Button.Face->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Button.Face->SetCastShadow(false);
	// 第二行是空的（START、SETTINGS 这种）就把第一行放在正中间
	const float Offset = Line2.IsEmpty() ? 0.f : Height * 0.22f;
	Button.Line1 = MakeText(*(Id + TEXT("Line1")), FVector(FaceX + 0.5f, Y, Z + Offset), Line1, TextSize, FColor::White);
	Button.Line2 = MakeText(*(Id + TEXT("Line2")), FVector(FaceX + 0.5f, Y, Z - Offset), Line2, TextSize * 0.9f, FColor(255, 230, 150));

	// 触发盒和触屏一样：从按钮面里 0.8cm 延伸到面前 1.6cm
	UBoxComponent* Zone = CreateDefaultSubobject<UBoxComponent>(*(Id + TEXT("Zone")));
	Zone->SetupAttachment(Pivot);
	Zone->SetRelativeLocation(FVector(FaceX + 0.8f, Y, Z));
	Zone->InitBoxExtent(FVector(1.2f, Width * 0.5f, Height * 0.5f));
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionObjectType(ECC_WorldDynamic);
	Zone->SetCollisionResponseToAllChannels(ECR_Overlap);
	Zone->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Button.Zone = Zone;
}

void AToyMenuPanel::BeginPlay()
{
	Super::BeginPlay();
	ShowPage(EToyMenuPage::Main);
}

void AToyMenuPanel::Open()
{
	bOpen = true;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	ShowPage(EToyMenuPage::Main);
	PlaceInFrontOfPlayer();
	// 开局时头显的追踪（和桌面模式的相机高度）要过一两帧才到位，过一会儿再按头的位置摆一次
	GetWorldTimerManager().SetTimer(PlaceTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PlaceInFrontOfPlayer(); }),
		0.5f, false);
	if (UToyFeedback* System = UToyFeedback::Get(this))
	{
		FToyFeedbackParams Params;
		Params.Location = GetActorLocation();
		System->Play(EToyFeedback::MenuOpen, Params);
	}
}

void AToyMenuPanel::Close()
{
	if (bOpen)
	{
		if (UToyFeedback* System = UToyFeedback::Get(this))
		{
			FToyFeedbackParams Params;
			Params.Location = GetActorLocation();
			System->Play(EToyFeedback::MenuClose, Params);
		}
	}
	bOpen = false;
	GetWorldTimerManager().ClearTimer(PlaceTimer);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AToyMenuPanel::Toggle()
{
	if (bOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

void AToyMenuPanel::PlaceInFrontOfPlayer()
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!CameraManager)
	{
		return;
	}
	// 只用头的水平朝向：低着头叫出菜单，菜单也不会钻到地板下面
	const FVector Eye = CameraManager->GetCameraLocation();
	const FRotator Yaw(0.f, CameraManager->GetCameraRotation().Yaw, 0.f);
	// 小人国：小身体时面板整个缩小，离头的距离也一起缩
	const float Scale = ToyLilliput::GetScale();
	const FVector Location = Eye + Yaw.Vector() * Distance * Scale - FVector(0.f, 0.f, BelowEyes * Scale);
	// 面板的 +X 是正面，转 180° 朝着玩家
	SetActorLocationAndRotation(Location, FRotator(0.f, Yaw.Yaw + 180.f, 0.f));
	SetActorScale3D(FVector(Scale));
}

void AToyMenuPanel::ShowPage(EToyMenuPage Page)
{
	CurrentPage = Page;
	for (const FToyMenuButton& Button : Buttons)
	{
		const bool bShow = Button.Page == Page;
		Button.Face->SetVisibility(bShow);
		Button.Line1->SetVisibility(bShow);
		Button.Line2->SetVisibility(bShow);
		Button.Zone->SetCollisionEnabled(bShow ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	Title->SetText(FText::FromString(Page == EToyMenuPage::Main ? TEXT("TOY BOX") : TEXT("SETTINGS")));
	Title->SetWorldSize(Page == EToyMenuPage::Main ? 2.6f : 2.f);
	Hint->SetText(FText::FromString(Page == EToyMenuPage::Main
		? (ToyLilliput::IsEnabled() ? TEXT("POKE A BUTTON - LEFT Y: SHRINK / GROW - LEFT X: MENU") : TEXT("POKE A BUTTON WITH YOUR FINGER"))
		: TEXT("EACH PRESS CYCLES THE OPTION - SAVED AUTOMATICALLY")));
	RefreshLabels();
}

void AToyMenuPanel::RefreshLabels()
{
	for (const FToyMenuButton& Button : Buttons)
	{
		FString Value;
		switch (Button.Action)
		{
		case EToyMenuAction::Start:      Button.Line1->SetText(FText::FromString(bStarted ? TEXT("RESUME") : TEXT("START"))); continue;
		case EToyMenuAction::Turn:       Value = ToySettings::TurnLabel(); break;
		case EToyMenuAction::MoveSpeed:  Value = ToySettings::MoveSpeedLabel(); break;
		case EToyMenuAction::Teleport:   Value = ToySettings::TeleportLabel(); break;
		case EToyMenuAction::PinchBall:  Value = ToySettings::PinchBallLabel(); break;
		case EToyMenuAction::GrabSize:   Value = ToySettings::GrabSizeLabel(); break;
		case EToyMenuAction::Resolution: Value = ToySettings::ResolutionLabel(); break;
		case EToyMenuAction::Sound:      Value = ToySettings::SoundLabel(); break;
		case EToyMenuAction::Haptics:    Value = ToySettings::HapticsLabel(); break;
		default: continue;
		}
		Button.Line2->SetText(FText::FromString(Value));
		// 第一次填字时把第一行挪到上半边（构造时第二行是空的，第一行放在了正中间）
		const FVector Rest = Button.RestLocation;
		Button.Line1->SetRelativeLocation(FVector(FaceX + 0.5f, Rest.Y, Rest.Z + SetHeight * 0.22f));
		Button.Line2->SetRelativeLocation(FVector(FaceX + 0.5f, Rest.Y, Rest.Z - SetHeight * 0.22f));
	}
}

bool AToyMenuPanel::Poke(UPrimitiveComponent* Touched)
{
	if (!bOpen)
	{
		return false;
	}
	const int32 Index = Buttons.IndexOfByPredicate([this, Touched](const FToyMenuButton& Button)
	{
		return Button.Zone.Get() == Touched && Button.Page == CurrentPage;
	});
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
	RunAction(Buttons[Index].Action);
	return true;
}

void AToyMenuPanel::RunAction(EToyMenuAction Action)
{
	switch (Action)
	{
	case EToyMenuAction::Start:
		bStarted = true;
		Close();
		return;
	case EToyMenuAction::OpenSettings: ShowPage(EToyMenuPage::Settings); return;
	case EToyMenuAction::Back:         ShowPage(EToyMenuPage::Main); return;
	case EToyMenuAction::Quit:
		UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
		return;
	case EToyMenuAction::CalibrateHands:
	{
		// 校准模式只在 VR 里有意义（摇杆调角度）；进去以后把菜单收起来，免得手在面板前乱戳
		AToyBoxPawn* Pawn = Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
		if (Pawn && Pawn->EnterHandCalibMode())
		{
			bStarted = true;
			Close();
		}
		else
		{
			Hint->SetText(FText::FromString(TEXT("HAND CALIBRATION NEEDS THE HEADSET")));
		}
		return;
	}
	case EToyMenuAction::Turn:       ToySettings::CycleTurn(); break;
	case EToyMenuAction::MoveSpeed:  ToySettings::CycleMoveSpeed(); break;
	case EToyMenuAction::Teleport:   ToySettings::ToggleTeleport(); break;
	case EToyMenuAction::PinchBall:  ToySettings::TogglePinchBall(); break;
	case EToyMenuAction::GrabSize:   ToySettings::CycleGrabSize(); break;
	case EToyMenuAction::Resolution: ToySettings::CycleResolution(); break;
	case EToyMenuAction::Sound:      ToySettings::CycleSound(); break;
	case EToyMenuAction::Haptics:    ToySettings::CycleHaptics(); break;
	}
	RefreshLabels();
}

void AToyMenuPanel::Flash(int32 ButtonIndex)
{
	// 和触屏一样：变白 + 往里缩一点，0.15 秒后恢复
	const FToyMenuButton& Button = Buttons[ButtonIndex];
	Button.Face->SetColor(FLinearColor::White);
	Button.Face->SetRelativeLocation(Button.RestLocation - FVector(0.5f, 0.f, 0.f));

	GetWorldTimerManager().SetTimer(FlashTimer, FTimerDelegate::CreateWeakLambda(this, [this, ButtonIndex]()
	{
		const FToyMenuButton& Pressed = Buttons[ButtonIndex];
		Pressed.Face->SetColor(Pressed.Color);
		Pressed.Face->SetRelativeLocation(Pressed.RestLocation);
	}), 0.15f, false);
}
