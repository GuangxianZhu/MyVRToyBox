#include "ToyPropTest.h"

#include "ToyBoxPawn.h"
#include "ToyHand.h"
#include "ToyMenuPanel.h"
#include "ToyProp.h"
#include "ToyStudio.h"
#include "ToyTouchScreen.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ToyPropTestLocal
{
	FAutoConsoleCommandWithWorldAndArgs PropTestCommand(
		TEXT("toy.PropTest"),
		TEXT("把样板间的摆件全部自检一遍（静止、叫醒、抓起提 20cm、松手落回、丢到地上、摆件复位、帧时间），截图到 Saved/ArchVizCheck/。参数 quit = 跑完退出"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<AToyPropTest> It(World); It; ++It)
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox: PropTest 已经在跑了"));
				return;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AToyPropTest* Test = World->SpawnActor<AToyPropTest>(Params))
			{
				Test->Start(Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); }));
			}
		}));

	/** 叫醒后、松手落定后，离该在的位置超过这么多（厘米）算动了 */
	constexpr float StillTolerance = 1.f;
	constexpr float LandTolerance = 3.f;
	constexpr float LiftCm = 20.f;
}

using namespace ToyPropTestLocal;

AToyPropTest::AToyPropTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AToyPropTest::Start(bool bInQuitWhenDone)
{
	bQuitWhenDone = bInQuitWhenDone;
	OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ArchVizCheck"));
	IFileManager::Get().MakeDirectory(*OutDir, true);
	UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest 开始"));
}

void AToyPropTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AToyBoxPawn* P = Pawn.Get())
	{
		P->SetExternalControl(false);
	}
	Super::EndPlay(EndPlayReason);
}

void AToyPropTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bMeasuring)
	{
		FrameTimeSum += DeltaSeconds;
		++FrameCount;
	}
	if (!bStarted)
	{
		// -ExecCmds 可能在玩家生成之前就执行了：等 Pawn、手和工作室都出来
		for (TActorIterator<AToyBoxPawn> It(GetWorld()); It; ++It)
		{
			Pawn = *It;
		}
		for (TActorIterator<AToyStudio> It(GetWorld()); It; ++It)
		{
			Studio = *It;
		}
		if (!Pawn.IsValid() || !Studio.IsValid() || !Pawn->GetRightHand() || GetWorld()->GetTimeSeconds() < 1.0)
		{
			return;
		}
		bStarted = true;
		Plan();
		return;
	}
	Elapsed += DeltaSeconds;
	while (Steps.IsValidIndex(StepIndex) && Elapsed >= Steps[StepIndex].Delay)
	{
		Elapsed = 0.f;
		const int32 Current = StepIndex++;
		Steps[Current].Run();
	}
}

void AToyPropTest::Then(float Delay, TFunction<void()> Step)
{
	Steps.Add({ Delay, MoveTemp(Step) });
}

float AToyPropTest::SupportZ(const AToyProp* Prop) const
{
	// 从摆件中心往下打线（不算摆件自己和别的摆件），打到的第一个面就是它放在上面的台面
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyPropTestSupport), true);
	for (const TWeakObjectPtr<AToyProp>& Other : Props)
	{
		Params.AddIgnoredActor(Other.Get());
	}
	const FVector From = Prop->Mesh->Bounds.Origin;
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
		? static_cast<float>(Hit.ImpactPoint.Z) : -1000.f;
}

FVector AToyPropTest::Center(const AToyProp* Prop) const
{
	return Prop->Mesh->Bounds.Origin;
}

void AToyPropTest::Fail(const FString& What)
{
	++Failures;
	UE_LOG(LogTemp, Warning, TEXT("ToyBox: PropTest 问题：%s"), *What);
}

void AToyPropTest::Shot(const FString& Name)
{
	FScreenshotRequest::RequestScreenshot(OutDir / (Name + TEXT(".png")), false, false);
}

void AToyPropTest::AimAt(const FVector& Target, float Distance, float Height)
{
	// 从站立点那边看过去（摆件都靠墙放，从房间里看才不会被墙挡住）
	AToyBoxPawn* P = Pawn.Get();
	const AToyStudio* S = Studio.Get();
	if (!P || !S)
	{
		return;
	}
	FVector Flat = Target - S->GetActorLocation();
	Flat.Z = 0.f;
	Flat = Flat.GetSafeNormal();
	const FVector Eye = Target - Flat * Distance + FVector(0.f, 0.f, Height);
	P->Camera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());
}

void AToyPropTest::Plan()
{
	AToyBoxPawn* P = Pawn.Get();
	P->SetExternalControl(true);
	for (TActorIterator<AToyMenuPanel> It(GetWorld()); It; ++It)
	{
		It->Close();
	}
	CameraOriginal = P->Camera->GetRelativeTransform();
	P->GetRightHand()->AddTickPrerequisiteActor(this);
	for (TActorIterator<AToyProp> It(GetWorld()); It; ++It)
	{
		Props.Add(*It);
	}
	// 按生成脚本起的标签排（Prop01_…），截图编号和 README 的表对得上
	auto SortKey = [](const TWeakObjectPtr<AToyProp>& Prop)
	{
#if WITH_EDITOR
		return Prop->GetActorLabel();
#else
		return Prop->GetName();
#endif
	};
	Props.Sort([SortKey](const TWeakObjectPtr<AToyProp>& A, const TWeakObjectPtr<AToyProp>& B) { return SortKey(A) < SortKey(B); });
	for (const TWeakObjectPtr<AToyProp>& Prop : Props)
	{
		Homes.Add(Prop->GetActorTransform());
		HomeBottoms.Add(SupportZ(Prop.Get()));
		Rows.Add(FString());
	}
	UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest 关卡里有 %d 件摆件"), Props.Num());
	auto AimScreen = [this]()
	{
		for (TActorIterator<AToyTouchScreen> It(GetWorld()); It; ++It)
		{
			const FVector Target = It->GetActorLocation();
			const FVector Eye = Studio->GetActorTransform().TransformPosition(FVector(0.f, 0.f, 160.f));
			Pawn->Camera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());
		}
	};
	if (Props.Num() == 0)
	{
		// 没有摆件的房间（L_Studio）：触屏应该写着"这个房间没有能拿起来的摆件"，按复位也不出事
		Then(1.f, AimScreen);
		Then(0.3f, [this]() { Studio->HandleScreenAction(EToyScreenAction::ResetProps); });
		Then(0.6f, [this]() { Shot(TEXT("props_screen_empty_room")); });
		Then(0.5f, [this]() { Finish(); });
		return;
	}

	// ---- 1. 开局静止；全部叫醒看会不会自己动 ----
	auto Moved = [this](int32 I)
	{
		const AToyProp* Prop = Props[I].Get();
		return Prop ? static_cast<float>(FVector::Dist(Prop->GetActorLocation(), Homes[I].GetLocation())) : -1.f;
	};
	TSharedRef<TArray<float>> Still = MakeShared<TArray<float>>();
	TSharedRef<TArray<float>> Woken = MakeShared<TArray<float>>();
	Then(2.f, [this, Moved, Still]()
	{
		for (int32 I = 0; I < Props.Num(); ++I)
		{
			Still->Add(Moved(I));
			if ((*Still)[I] > StillTolerance)
			{
				Fail(FString::Printf(TEXT("%s 开局 2 秒就离开原位 %.2fcm"), *Props[I]->DisplayName, (*Still)[I]));
			}
		}
		for (const TWeakObjectPtr<AToyProp>& Prop : Props)
		{
			Prop->Mesh->WakeRigidBody();
		}
	});
	Then(2.f, [this, Moved, Still, Woken]()
	{
		for (int32 I = 0; I < Props.Num(); ++I)
		{
			Woken->Add(Moved(I));
			const float Turn = FMath::RadiansToDegrees(Props[I]->GetActorQuat().AngularDistance(Homes[I].GetRotation()));
			UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest %s 开局 %.2fcm，叫醒 2 秒后 %.2fcm / %.1f°"), *Props[I]->DisplayName, (*Still)[I], (*Woken)[I], Turn);
			if ((*Woken)[I] > StillTolerance || Turn > 2.f)
			{
				Fail(FString::Printf(TEXT("%s 叫醒后自己动了 %.2fcm / %.1f°（碰撞和旁边的东西顶着，或者底下没有碰撞）"), *Props[I]->DisplayName, (*Woken)[I], Turn));
			}
		}
		Studio->ResetProps();
	});

	// ---- 2. 一件一件抓 ----
	TSharedRef<TArray<FString>> Cells = MakeShared<TArray<FString>>();
	for (int32 I = 0; I < Props.Num(); ++I)
	{
		Then(0.f, [this, I, Still, Woken]()
		{
			const AToyProp* Prop = Props[I].Get();
			const FVector Size = Prop->Mesh->Bounds.BoxExtent * 2.f;
			Rows[I] = FString::Printf(TEXT("%d,%s,%s,%.2f,%.0fx%.0fx%.0f,%.2f,%.2f"), I + 1, *Prop->DisplayName, *Prop->MaterialText,
				Prop->Mesh->GetMass(), Size.X, Size.Y, Size.Z, (*Still)[I], (*Woken)[I]);
		});
		PlanProp(I);
	}

	// ---- 3. 从 1 米高丢到站立点身后的空地上 ----
	TSharedRef<TArray<float>> FloorZ = MakeShared<TArray<float>>();
	Then(0.5f, [this]()
	{
		const FTransform Frame = Studio->GetActorTransform();
		for (int32 I = 0; I < Props.Num(); ++I)
		{
			AToyProp* Prop = Props[I].Get();
			const float Y = (I - (Props.Num() - 1) * 0.5f) * 32.f;
			const FVector Drop = Frame.TransformPosition(FVector(-100.f, Y, 100.f));
			Prop->SetActorLocation(Drop + (Prop->GetActorLocation() - Center(Prop)), false, nullptr, ETeleportType::ResetPhysics);
			Prop->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Prop->Mesh->WakeRigidBody();
		}
	});
	Then(3.f, [this, FloorZ]()
	{
		const float Floor = Studio->GetActorLocation().Z;
		for (int32 I = 0; I < Props.Num(); ++I)
		{
			const AToyProp* Prop = Props[I].Get();
			// 摆件下面打到的东西离地面多高（地毯约 1.7cm）；摆件中心要在它上面
			const float Bottom = SupportZ(Prop) - Floor;
			const float Above = static_cast<float>(Prop->Mesh->Bounds.Origin.Z) - Floor;
			FloorZ->Add(Bottom);
			UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest %s 丢到地上：落在离地 %.1fcm 的面上，中心离地 %.1fcm"), *Prop->DisplayName, Bottom, Above);
			if (Bottom < -0.5f || Bottom > 3.f || Above < 0.f || Above > 30.f)
			{
				Fail(FString::Printf(TEXT("%s 丢到地上后落在离地 %.1fcm 的面上、中心离地 %.1fcm（穿过地板或卡在半空）"), *Prop->DisplayName, Bottom, Above));
			}
			Rows[I] += FString::Printf(TEXT(",%.1f"), Bottom);
		}
		AimAt(Studio->GetActorTransform().TransformPosition(FVector(-100.f, 0.f, 10.f)), 190.f, 120.f);
	});
	Then(0.6f, [this]() { Shot(TEXT("props_floor")); });

	// ---- 4. 触屏「摆件复位」 ----
	Then(0.5f, AimScreen);
	Then(0.6f, [this]() { Shot(TEXT("props_screen_away")); });
	Then(0.3f, [this]()
	{
		// 和戳触屏上的按钮走同一个入口
		Studio->HandleScreenAction(EToyScreenAction::ResetProps);
	});
	Then(1.f, [this]()
	{
		int32 Away = 0;
		for (int32 I = 0; I < Props.Num(); ++I)
		{
			const bool bAway = Props[I]->IsAwayFromHome();
			Away += bAway ? 1 : 0;
			Rows[I] += bAway ? TEXT(",away") : TEXT(",ok");
			if (bAway)
			{
				Fail(FString::Printf(TEXT("%s 摆件复位后不在原位"), *Props[I]->DisplayName));
			}
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest 摆件复位：%d 件里 %d 件不在原位"), Props.Num(), Away);
		Shot(TEXT("props_screen_reset"));
	});

	// ---- 5. 回到原来的镜头看 3 秒，记帧时间 ----
	Then(0.5f, [this]()
	{
		Pawn->Camera->SetRelativeTransform(CameraOriginal);
	});
	Then(2.f, [this]()
	{
		bMeasuring = true;
		FrameTimeSum = 0.0;
		FrameCount = 0;
	});
	Then(3.f, [this]()
	{
		bMeasuring = false;
		UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest 帧时间（开局镜头，摆件都在原位）平均 %.2f ms，%d 帧"),
			FrameCount ? FrameTimeSum / FrameCount * 1000.0 : 0.0, FrameCount);
		Finish();
	});
}

void AToyPropTest::PlanProp(int32 I)
{
	TSharedRef<FVector> Grabbed = MakeShared<FVector>();
	Then(0.3f, [this, I]()
	{
		AToyProp* Prop = Props[I].Get();
		AToyHand* Hand = Pawn->GetRightHand();
		const float Size = static_cast<float>(Prop->Mesh->Bounds.BoxExtent.GetMax()) * 2.f;
		AimAt(Center(Prop), 45.f + Size * 1.5f, 25.f + Size * 0.5f);
		// 和桌面模式鼠标点中一样：捏合点放在摆件上离镜头最近的表面点，手指沿视线水平方向、掌心朝下
		const FVector Eye = Pawn->Camera->GetComponentLocation();
		FVector Point = Center(Prop);
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyPropTest), true);
		if (Prop->Mesh->LineTraceComponent(Hit, Eye, Point + (Point - Eye).GetSafeNormal() * 50.f, Params))
		{
			Point = Hit.ImpactPoint;
		}
		Hand->SetPreferredTarget(Prop);
		Hand->SetInputs(0.f, 0.f);
		Hand->SetDesktopPose(Point, FRotator(0.f, (Point - Eye).Rotation().Yaw, -90.f));
	});
	Then(0.4f, [this, I]()
	{
		const bool bHover = Props[I]->Mesh->GetOverlayMaterial() != nullptr;
		Rows[I] += bHover ? TEXT(",1") : TEXT(",0");
		if (!bHover)
		{
			Fail(FString::Printf(TEXT("%s 手伸过去没有高亮"), *Props[I]->DisplayName));
		}
		Shot(FString::Printf(TEXT("props_%02d_hover"), I + 1));
	});
	Then(0.3f, [this]() { Pawn->GetRightHand()->SetInputs(0.f, 1.f); });
	Then(0.4f, [this, I, Grabbed]()
	{
		AToyHand* Hand = Pawn->GetRightHand();
		AToyProp* Prop = Props[I].Get();
		const bool bHeld = Hand->GetHeldActor() == Prop;
		Rows[I] += bHeld ? TEXT(",1") : TEXT(",0");
		*Grabbed = Prop->GetActorLocation();
		if (!bHeld)
		{
			Fail(FString::Printf(TEXT("%s 没抓到（手里是 %s）"), *Prop->DisplayName, *GetNameSafe(Hand->GetHeldActor())));
			return;
		}
		Hand->SetActorLocation(Hand->GetActorLocation() + FVector(0.f, 0.f, LiftCm));
	});
	Then(0.6f, [this, I, Grabbed]()
	{
		AToyProp* Prop = Props[I].Get();
		const float Lift = static_cast<float>(Prop->GetActorLocation().Z - Grabbed->Z);
		Rows[I] += FString::Printf(TEXT(",%.1f"), Lift);
		if (Pawn->GetRightHand()->GetHeldActor() == Prop && FMath::Abs(Lift - LiftCm) > 2.f)
		{
			Fail(FString::Printf(TEXT("%s 手提了 %.0fcm，摆件只跟了 %.1fcm"), *Prop->DisplayName, LiftCm, Lift));
		}
		const float Size = static_cast<float>(Prop->Mesh->Bounds.BoxExtent.GetMax()) * 2.f;
		AimAt(Center(Prop), 50.f + Size * 1.5f, 15.f);
		Shot(FString::Printf(TEXT("props_%02d_held"), I + 1));
	});
	// 手提起来后停够 0.5 秒再松（扔出去的速度按最近 6 帧算，停住了就是轻轻放手）
	Then(0.6f, [this]()
	{
		AToyHand* Hand = Pawn->GetRightHand();
		Hand->SetInputs(0.f, 0.f);
		Hand->SetPreferredTarget(nullptr);
	});
	Then(2.5f, [this, I]()
	{
		AToyProp* Prop = Props[I].Get();
		// 下面打到的台面和开局时的一样高 = 落回原来的台面（倒下、滚开一点都算）
		const float Dz = SupportZ(Prop) - HomeBottoms[I];
		const float Dxy = static_cast<float>(FVector::Dist2D(Prop->GetActorLocation(), Homes[I].GetLocation()));
		Rows[I] += FString::Printf(TEXT(",%.1f"), Dz);
		UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest %s 松手落定：落在比原来高 %.1fcm 的面上、水平偏 %.1fcm，速度 %.1fcm/s"), *Prop->DisplayName, Dz, Dxy,
			Prop->Mesh->GetPhysicsLinearVelocity().Size());
		if (FMath::Abs(Dz) > LandTolerance)
		{
			Fail(FString::Printf(TEXT("%s 松手后没落回原来的台面（高差 %.1fcm）"), *Prop->DisplayName, Dz));
		}
		Studio->ResetProps();
	});
}

void AToyPropTest::Finish()
{
	const FString Path = OutDir / TEXT("props_summary.csv");
	const FString Header = TEXT("index,name,material,mass_kg,size_cm,still_cm,woken_cm,hover,grabbed,lift_cm,landed_support_dz_cm,floor_support_cm,reset");
	FFileHelper::SaveStringToFile(Header + TEXT("\n") + FString::Join(Rows, TEXT("\n")) + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	UE_LOG(LogTemp, Log, TEXT("ToyBox: PropTest 完成：%d 件，问题 %d 个，结果 %s"), Props.Num(), Failures, *Path);
	if (AToyBoxPawn* P = Pawn.Get())
	{
		P->SetExternalControl(false);
		P->Camera->SetRelativeTransform(CameraOriginal);
		if (AToyHand* Hand = P->GetRightHand())
		{
			Hand->RemoveTickPrerequisiteActor(this);
		}
	}
	if (bQuitWhenDone)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 1.f, false);
		return;
	}
	Destroy();
}
