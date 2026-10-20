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
	const FLinearColor SpawnColor(0.1f, 0.7f, 0.25f);
	const FLinearColor ClearColor(0.8f, 0.15f, 0.12f);
	// 按钮面的中心（屏幕坐标），按下时往里缩 0.5cm
	constexpr float FaceX = 1.8f;
	constexpr float ButtonZ = -2.f;
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
	MakeText(TEXT("Title"), FVector(1.5f, 0.f, 7.5f), TEXT("TOY BOX"), 2.6f, FColor::White);
	Status = MakeText(TEXT("Status"), FVector(1.5f, 0.f, -8.3f), TEXT("CUBES 0"), 1.5f, FColor(150, 200, 255));

	// 站在屏幕前看，+Y 在左手边
	SpawnZone = MakeButton(TEXT("Spawn"), 7.5f, TEXT("SPAWN"), TEXT("CUBE"), SpawnColor, SpawnFace);
	ClearZone = MakeButton(TEXT("Clear"), -7.5f, TEXT("CLEAR"), TEXT("ALL"), ClearColor, ClearFace);
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

UBoxComponent* AToyTouchScreen::MakeButton(const FString& Id, float Y, const FString& Line1, const FString& Line2,
	const FLinearColor& Color, TObjectPtr<UToyShapeComponent>& OutFace)
{
	OutFace = UToyShapeComponent::Create(this, *(Id + TEXT("Face")), Pivot, EToyShape::Box, FVector(0.8f, 13.f, 8.f),
		FVector(FaceX, Y, ButtonZ), Color, FRotator::ZeroRotator);
	MakeText(*(Id + TEXT("Line1")), FVector(FaceX + 0.5f, Y, ButtonZ + 1.4f), Line1, 2.2f, FColor::White);
	MakeText(*(Id + TEXT("Line2")), FVector(FaceX + 0.5f, Y, ButtonZ - 1.4f), Line2, 2.2f, FColor::White);

	// 触发盒从按钮面里 0.8cm 一直延伸到面前 1.6cm：指尖快碰到就算按下，戳穿了也还在盒子里
	UBoxComponent* Zone = CreateDefaultSubobject<UBoxComponent>(*(Id + TEXT("Zone")));
	Zone->SetupAttachment(Pivot);
	Zone->SetRelativeLocation(FVector(FaceX + 0.8f, Y, ButtonZ));
	Zone->InitBoxExtent(FVector(1.2f, 6.5f, 4.f));
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionObjectType(ECC_WorldDynamic);
	Zone->SetCollisionResponseToAllChannels(ECR_Overlap);
	// 桌面模式用 Visibility 射线点按钮
	Zone->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	return Zone;
}

void AToyTouchScreen::SetStudio(AToyStudio* InStudio)
{
	Studio = InStudio;
	RefreshStatus();
}

bool AToyTouchScreen::Poke(UPrimitiveComponent* Touched)
{
	const bool bSpawn = Touched == SpawnZone.Get();
	const bool bClear = Touched == ClearZone.Get();
	if (!bSpawn && !bClear)
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPressTime < PressCooldown)
	{
		return false;
	}
	LastPressTime = Now;

	AToyStudio* StudioActor = Studio.Get();
	if (bSpawn)
	{
		Flash(SpawnFace, SpawnColor);
		if (StudioActor)
		{
			StudioActor->SpawnCube();
		}
	}
	else
	{
		Flash(ClearFace, ClearColor);
		if (StudioActor)
		{
			StudioActor->ClearCubes();
		}
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
	const int32 Count = StudioActor->GetCubeCount();
	const FString Text = Count >= StudioActor->MaxCubes
		? FString::Printf(TEXT("FULL %d / %d  -  CLEAR FIRST"), Count, StudioActor->MaxCubes)
		: FString::Printf(TEXT("CUBES %d / %d"), Count, StudioActor->MaxCubes);
	Status->SetText(FText::FromString(Text));
}

void AToyTouchScreen::Flash(UToyShapeComponent* Face, const FLinearColor& Color)
{
	// 按下反馈：变白 + 往里缩一点，0.15 秒后恢复（冷却 0.35 秒，所以不会叠加）
	const FVector Rest = Face->GetRelativeLocation();
	Face->SetColor(FLinearColor::White);
	Face->SetRelativeLocation(Rest - FVector(0.5f, 0.f, 0.f));

	TWeakObjectPtr<UToyShapeComponent> WeakFace(Face);
	GetWorldTimerManager().SetTimer(FlashTimer, FTimerDelegate::CreateWeakLambda(this, [WeakFace, Color, Rest]()
	{
		if (UToyShapeComponent* Restored = WeakFace.Get())
		{
			Restored->SetColor(Color);
			Restored->SetRelativeLocation(Rest);
		}
	}), 0.15f, false);
}
