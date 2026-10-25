#include "ToyDemoRecorder.h"

#include "ToyBoxPawn.h"
#include "ToyExplodedModel.h"
#include "ToyHand.h"
#include "ToyMenuPanel.h"
#include "ToyPartUnit.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace
{
	void StartDemo(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || AToyDemoRecorder::IsRunning(World))
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: Demo 已经在录了（或者没有游戏世界）"));
			return;
		}
		TArray<EToyModelKind> Kinds;
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("Motor"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Motor);
			else if (Arg.Equals(TEXT("Valve"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Valve);
			else if (Arg.Equals(TEXT("Drone"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Drone);
			else if (Arg.Equals(TEXT("Lathe"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Lathe);
			else if (Arg.Equals(TEXT("Multimeter"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("Meter"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::Multimeter);
			else if (Arg.Equals(TEXT("Server"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("AIServer"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::Server);
			else if (Arg.Equals(TEXT("Robot"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("Orb"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::Orb);
			else if (Arg.Equals(TEXT("Engine"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::Engine);
			else if (Arg.Equals(TEXT("Hand"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("DexHand"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::DexHand);
			else if (Arg.Equals(TEXT("Etcher"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("Etch"), ESearchCase::IgnoreCase))
				Kinds.Add(EToyModelKind::Etcher);
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) bQuit = true;
		}
		if (Kinds.Num() == 0)
		{
			Kinds = { EToyModelKind::Motor, EToyModelKind::Valve, EToyModelKind::Drone, EToyModelKind::Lathe, EToyModelKind::Multimeter, EToyModelKind::Server, EToyModelKind::Orb, EToyModelKind::Engine, EToyModelKind::DexHand, EToyModelKind::Etcher };
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AToyDemoRecorder* Demo = World->SpawnActor<AToyDemoRecorder>(Params))
		{
			Demo->Start(Kinds, bQuit);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DemoCommand(
		TEXT("toy.Demo"),
		TEXT("录 README 演示短片（逐帧截图到 Saved/Demo/<模型>/）。参数：Motor / Valve / Drone / Lathe / Multimeter / Server / Robot / Engine / Hand / Etcher（不写 = 全部），quit = 录完退出。配合 -benchmark -fps=15"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartDemo));

	float Smooth(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	/** 不出镜的时候把手藏到桌子下面。 */
	const FVector HandParking(0.f, 0.f, -500.f);
}

AToyDemoRecorder::AToyDemoRecorder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AToyDemoRecorder::IsRunning(const UWorld* World)
{
	for (TActorIterator<AToyDemoRecorder> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return true;
	}
	return false;
}

void AToyDemoRecorder::Start(const TArray<EToyModelKind>& InKinds, bool bInQuitWhenDone)
{
	Kinds = InKinds;
	bQuitWhenDone = bInQuitWhenDone;
	Step = EStep::FindPlayer;
	UE_LOG(LogTemp, Log, TEXT("ToyBox: Demo 开始，%d 个模型"), Kinds.Num());
}

void AToyDemoRecorder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AToyBoxPawn* P = Pawn.Get())
	{
		P->SetExternalControl(false);
	}
	Super::EndPlay(EndPlayReason);
}

void AToyDemoRecorder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	if (Step != EStep::Record && Elapsed < Wait)
	{
		return;
	}

	AToyBoxPawn* P = Pawn.Get();
	switch (Step)
	{
	case EStep::FindPlayer:
	{
		for (TActorIterator<AToyBoxPawn> It(GetWorld()); It; ++It)
		{
			Pawn = *It;
		}
		for (TActorIterator<AToyStudio> It(GetWorld()); It; ++It)
		{
			Studio = *It;
		}
		if (Pawn.IsValid() && Studio.IsValid() && Pawn->GetRightHand())
		{
			Pawn->SetExternalControl(true);
			for (TActorIterator<AToyMenuPanel> It(GetWorld()); It; ++It)
			{
				It->Close();
			}
			CameraOriginal = Pawn->Camera->GetRelativeTransform();
			Pawn->GetRightHand()->AddTickPrerequisiteActor(this);
			NextModel();
		}
		else
		{
			Wait = 0.5f;
			Elapsed = 0.f;
		}
		break;
	}
	case EStep::ShowModel:
	{
		AToyStudio* S = Studio.Get();
		const EToyModelKind Kind = Kinds[KindIndex];
		if (!S || (!S->IsModelShown(Kind) && !S->ToggleModel(Kind)))
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: Demo 摆不出 %s，跳过"), *AToyStudio::KindName(Kind));
			NextModel();
			break;
		}
		Model = S->GetModel();
		OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demo") / AToyStudio::KindName(Kind));
		IFileManager::Get().DeleteDirectory(*OutDir, false, true);
		IFileManager::Get().MakeDirectory(*OutDir, true);
		if (AToyHand* Hand = P->GetRightHand())
		{
			Hand->SetInputs(0.f, 0.f);
			Hand->SetActorLocation(HandParking);
		}
		AssembledBounds = ExplodedBounds = FBox(ForceInit);
		AimCamera(0.f);
		Step = EStep::WaitShaders;
		Wait = 2.f;
		Elapsed = 0.f;
		break;
	}
	case EStep::WaitShaders:
	{
		// 第一次摆出模型时材质要编译着色器，编译完再开始录，不然开头几帧是默认灰材质
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
		{
			Wait = 1.f;
			Elapsed = 0.f;
			break;
		}
		AToyExplodedModel* M = Model.Get();
		if (!M)
		{
			NextModel();
			break;
		}
		AssembledBounds = UnitBounds();
		M->SetExploded(true);
		MeasurePhase = 0;
		Step = EStep::Measure;
		Wait = M->ExplodeDuration + 0.3f;
		Elapsed = 0.f;
		break;
	}
	case EStep::Measure:
	{
		AToyExplodedModel* M = Model.Get();
		if (!M)
		{
			NextModel();
			break;
		}
		if (MeasurePhase == 0)
		{
			ExplodedBounds = UnitBounds();
			M->SetExploded(false);
			MeasurePhase = 1;
			Wait = M->ExplodeDuration + 0.5f;
			Elapsed = 0.f;
			break;
		}
		const float D = M->ExplodeDuration;
		TExplode = 1.2f;
		TReach = TExplode + D + 0.6f;
		TLift = TReach + 1.0f;
		TRelease = TLift + 1.9f;
		TAssemble = TRelease + 0.9f;
		TEnd = TAssemble + D + 0.9f;
		PickFeatured();
		bGrabMeasured = bReleased = bAssembling = false;
		RecordTime = 0.f;
		Frame = 0;
		Step = EStep::Record;
		break;
	}
	case EStep::Record:
	{
		if (!ApplyTimeline(RecordTime))
		{
			UE_LOG(LogTemp, Log, TEXT("ToyBox: Demo %s 录完，%d 帧在 %s"), *AToyStudio::KindName(Kinds[KindIndex]), Frame, *OutDir);
			NextModel();
			break;
		}
		FScreenshotRequest::RequestScreenshot(OutDir / FString::Printf(TEXT("f_%04d.png"), Frame), false, false);
		++Frame;
		RecordTime += DeltaSeconds;
		break;
	}
	case EStep::Finish:
	default:
		break;
	}
}

void AToyDemoRecorder::PickFeatured()
{
	// 抓第二大的能抓的单元：最大的一般是机壳 / 底座，抓起来会挡住整个镜头
	TArray<TPair<double, AToyPartUnit*>> Sized;
	if (AToyExplodedModel* M = Model.Get())
	{
		for (AToyPartUnit* Unit : M->GetUnits())
		{
			if (Unit && Unit->IsGrabbableUnit())
			{
				Sized.Emplace(Unit->GetComponentsBoundingBox(false, true).GetVolume(), Unit);
			}
		}
	}
	Sized.Sort([](const TPair<double, AToyPartUnit*>& A, const TPair<double, AToyPartUnit*>& B) { return A.Key > B.Key; });
	Featured = Sized.Num() > 1 ? Sized[1].Value : (Sized.Num() ? Sized[0].Value : nullptr);
}

void AToyDemoRecorder::AimCamera(float Time)
{
	// 和 GrabAudit 的全景一样站在桌前偏左、比眼睛低一点斜着看，再随时间绕模型慢慢转 24°
	AToyBoxPawn* P = Pawn.Get();
	const AToyExplodedModel* M = Model.Get();
	if (!P || !M)
	{
		return;
	}
	const float Alpha = TEnd > 0.f ? FMath::Clamp(Time / TEnd, 0.f, 1.f) : 0.f;
	// 爆炸进度（按时间线算）：取景在装配 / 爆炸两个包围盒之间过渡
	const float D = FMath::Max(M->ExplodeDuration, 0.1f);
	const float Explode = Time < TAssemble ? Smooth((Time - TExplode) / D) : 1.f - Smooth((Time - TAssemble) / D);
	const FBox& From = AssembledBounds.IsValid ? AssembledBounds : ExplodedBounds;
	const FBox& To = ExplodedBounds.IsValid ? ExplodedBounds : From;
	const FVector Back = FRotator(0.f, FMath::Lerp(-12.f, 12.f, Alpha), 0.f).RotateVector(FVector(-62.f, -34.f, 40.f).GetSafeNormal());
	FVector Target = M->GetActorLocation() + FVector(0.f, 0.f, 12.f);
	float Distance = 82.f;   // 和 GrabAudit 全景一样的距离 (-62, -34, 40)
	if (From.IsValid)
	{
		const FVector Min = FMath::Lerp(From.Min, To.Min, Explode);
		const FVector Max = FMath::Lerp(From.Max, To.Max, Explode);
		Target = (Min + Max) * 0.5f;
		// 让包围盒 8 个角都落在画面里（留 5% 边）：角在镜头系里横向 y、纵向 z、纵深 x，要 |y| ≤ (d + x)·tan(半水平角)
		const FVector Forward = -Back;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Forward, Right);
		const float TanH = FMath::Tan(FMath::DegreesToRadians(P->Camera->FieldOfView * 0.5f)) * 0.95f;
		const float TanV = TanH * 9.f / 16.f;
		Distance = 30.f;
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector Corner = FVector(I & 1 ? Max.X : Min.X, I & 2 ? Max.Y : Min.Y, I & 4 ? Max.Z : Min.Z) - Target;
			const float X = FVector::DotProduct(Corner, Forward);
			Distance = FMath::Max(Distance, FMath::Abs(FVector::DotProduct(Corner, Right)) / TanH - X);
			Distance = FMath::Max(Distance, FMath::Abs(FVector::DotProduct(Corner, Up)) / TanV - X);
		}
	}
	const FVector Eye = Target + Back * Distance;
	P->Camera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());
}

FBox AToyDemoRecorder::UnitBounds() const
{
	FBox Box(ForceInit);
	if (const AToyExplodedModel* M = Model.Get())
	{
		for (const AToyPartUnit* Unit : M->GetUnits())
		{
			if (Unit)
			{
				Box += Unit->GetComponentsBoundingBox(false, true);
			}
		}
	}
	return Box;
}

bool AToyDemoRecorder::ApplyTimeline(float Time)
{
	AToyBoxPawn* P = Pawn.Get();
	AToyExplodedModel* M = Model.Get();
	AToyHand* Hand = P ? P->GetRightHand() : nullptr;
	if (!P || !M || !Hand || Time > TEnd)
	{
		return false;
	}
	AimCamera(Time);
	const FVector Eye = P->Camera->GetComponentLocation();

	if (Time >= TExplode && !M->IsExploded() && !bAssembling)
	{
		M->SetExploded(true);
	}

	AToyPartUnit* Unit = Featured.Get();
	if (Unit && Time >= TReach && !bGrabMeasured)
	{
		// 和 GrabAudit 一样：瞄准包围盒里 3×3×3 个点，取离镜头最近的命中点
		const FVector Center = Unit->GetActorTransform().TransformPosition(Unit->GetLocalCenter());
		const FBox Bounds = Unit->GetComponentsBoundingBox(false, true);
		GrabPoint = Center;
		float Best = TNumericLimits<float>::Max();
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyDemo), true);
		for (int32 I = 0; I < 27; ++I)
		{
			const FVector Aim = Center + Bounds.GetExtent() * 0.5f * FVector(I % 3 - 1, (I / 3) % 3 - 1, I / 9 - 1);
			for (UStaticMeshComponent* Mesh : Unit->GetMeshes())
			{
				FHitResult Hit;
				if (Mesh && Mesh->LineTraceComponent(Hit, Eye, Aim + (Aim - Eye).GetSafeNormal() * 50.f, Params) && Hit.Distance < Best)
				{
					Best = Hit.Distance;
					GrabPoint = Hit.ImpactPoint;
				}
			}
		}
		const FVector Direction = (GrabPoint - Eye).GetSafeNormal();
		HandFacing = FRotator(0.f, Direction.Rotation().Yaw, -90.f);
		// 从镜头右下方伸进画面
		ReachFrom = FMath::Lerp(GrabPoint, Eye, 0.6f) + P->Camera->GetRightVector() * 12.f - FVector(0.f, 0.f, 14.f);
		Hand->SetPreferredTarget(Unit);
		bGrabMeasured = true;
	}

	if (!bGrabMeasured || Time < TReach)
	{
		Hand->SetInputs(0.f, 0.f);
		Hand->SetActorLocation(HandParking);
	}
	else if (Time < TReach + 0.6f)
	{
		Hand->SetInputs(0.f, 0.f);
		Hand->SetDesktopPose(FMath::Lerp(ReachFrom, GrabPoint, Smooth((Time - TReach) / 0.6f)), HandFacing);
	}
	else if (Time < TLift)
	{
		// 扣扳机 = 捏
		Hand->SetDesktopPose(GrabPoint, HandFacing);
		Hand->SetInputs(0.f, Smooth((Time - TReach - 0.6f) / 0.25f));
	}
	else if (Time < TRelease)
	{
		Hand->SetInputs(0.f, 1.f);
		if (HeldHandStart.IsZero())
		{
			HeldHandStart = Hand->GetActorLocation();
		}
		// 拿起来往上、往镜头这边提，让信息牌浮出来
		const FVector Toward = (Eye - GrabPoint).GetSafeNormal2D();
		const float A = Smooth((Time - TLift) / 0.7f);
		Hand->SetActorLocation(HeldHandStart + A * (FVector(0.f, 0.f, 16.f) + Toward * 10.f));
	}
	else
	{
		if (!bReleased)
		{
			Hand->SetInputs(0.f, 0.f);
			Hand->SetPreferredTarget(nullptr);
			bReleased = true;
		}
		// 松手后手退回镜头右下方，再藏起来；零件自己飞回原位
		const float A = Smooth((Time - TRelease) / 0.5f);
		if (A < 1.f)
		{
			const FVector From = HeldHandStart + FVector(0.f, 0.f, 16.f) + (Eye - GrabPoint).GetSafeNormal2D() * 10.f;
			Hand->SetActorLocation(FMath::Lerp(From, From + (ReachFrom - GrabPoint), A));
		}
		else
		{
			Hand->SetActorLocation(HandParking);
		}
	}

	if (Time >= TAssemble && !bAssembling)
	{
		M->SetExploded(false);
		bAssembling = true;
	}
	return true;
}

void AToyDemoRecorder::NextModel()
{
	HeldHandStart = FVector::ZeroVector;
	++KindIndex;
	if (!Kinds.IsValidIndex(KindIndex))
	{
		Finish();
		return;
	}
	Step = EStep::ShowModel;
	Wait = 0.2f;
	Elapsed = 0.f;
}

void AToyDemoRecorder::Finish()
{
	Step = EStep::Finish;
	UE_LOG(LogTemp, Log, TEXT("ToyBox: Demo 全部录完，在 %s"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demo")));
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
		// 等最后一张截图写完
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}), 1.f, false);
		return;
	}
	Destroy();
}
