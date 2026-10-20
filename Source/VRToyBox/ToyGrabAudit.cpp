#include "ToyGrabAudit.h"

#include "ToyBoxPawn.h"
#include "ToyExplodedModel.h"
#include "ToyHand.h"
#include "ToyPartUnit.h"
#include "Camera/CameraComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace
{
	void StartAudit(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || AToyGrabAudit::IsRunning(World))
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: GrabAudit 已经在跑了（或者没有游戏世界）"));
			return;
		}
		TArray<EToyModelKind> Kinds;
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("Motor"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Motor);
			else if (Arg.Equals(TEXT("Valve"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Valve);
			else if (Arg.Equals(TEXT("Drone"), ESearchCase::IgnoreCase)) Kinds.Add(EToyModelKind::Drone);
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) bQuit = true;
		}
		if (Kinds.Num() == 0)
		{
			Kinds = { EToyModelKind::Motor, EToyModelKind::Valve, EToyModelKind::Drone };
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AToyGrabAudit* Audit = World->SpawnActor<AToyGrabAudit>(Params))
		{
			Audit->Start(Kinds, bQuit);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GrabAuditCommand(
		TEXT("toy.GrabAudit"),
		TEXT("自动把爆炸图的每个抓取单元抓一遍并截图到 Saved/GrabAudit/。参数：Motor / Valve / Drone（不写 = 全部），quit = 跑完退出"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartAudit));

	FString CsvField(const FString& Text)
	{
		return TEXT("\"") + Text.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
	}
}

AToyGrabAudit::AToyGrabAudit()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AToyGrabAudit::IsRunning(const UWorld* World)
{
	for (TActorIterator<AToyGrabAudit> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return true;
	}
	return false;
}

void AToyGrabAudit::Start(const TArray<EToyModelKind>& InKinds, bool bInQuitWhenDone)
{
	Kinds = InKinds;
	bQuitWhenDone = bInQuitWhenDone;
	Rows.Add(TEXT("model,index,unit,name,grab,grabbed,palm_gap_cm,pinch_gap_cm,screenshot"));
	Goto(EStep::FindPlayer);
	UE_LOG(LogTemp, Log, TEXT("ToyBox: GrabAudit 开始，%d 个模型"), Kinds.Num());
}

void AToyGrabAudit::Goto(EStep Next, float Delay)
{
	Step = Next;
	Wait = Delay;
	Elapsed = 0.f;
}

void AToyGrabAudit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AToyBoxPawn* P = Pawn.Get())
	{
		P->SetExternalControl(false);
	}
	Super::EndPlay(EndPlayReason);
}

void AToyGrabAudit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	if (Elapsed < Wait)
	{
		return;
	}

	AToyBoxPawn* P = Pawn.Get();
	AToyHand* Hand = P ? P->GetRightHand() : nullptr;
	switch (Step)
	{
	case EStep::FindPlayer:
	{
		// -ExecCmds 可能在玩家生成之前就执行了：等 Pawn 和工作室都出来
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
			CameraOriginal = Pawn->Camera->GetRelativeTransform();
			Pawn->GetRightHand()->AddTickPrerequisiteActor(this);
			NextModel();
		}
		else
		{
			Goto(EStep::FindPlayer, 0.5f);
		}
		break;
	}
	case EStep::ShowModel:
	{
		AToyStudio* S = Studio.Get();
		const EToyModelKind Kind = Kinds[KindIndex];
		if (!S || (!S->IsModelShown(Kind) && !S->ToggleModel(Kind)))
		{
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: GrabAudit 摆不出 %s，跳过"), *AToyStudio::KindName(Kind));
			NextModel();
			break;
		}
		Model = S->GetModel();
		OutDir = FPaths::ProjectSavedDir() / TEXT("GrabAudit") / AToyStudio::KindName(Kind);
		IFileManager::Get().MakeDirectory(*OutDir, true);
		// 镜头对准模型（模型原点在底面，往上一点是中间）
		const FVector Target = Model->GetActorLocation() + FVector(0.f, 0.f, 12.f);
		P->Camera->SetRelativeTransform(CameraOriginal);
		P->Camera->SetWorldRotation((Target - P->Camera->GetComponentLocation()).Rotation());
		CameraHome = P->Camera->GetRelativeTransform();
		Goto(EStep::WaitModel, 1.f);
		break;
	}
	case EStep::WaitModel:
	{
		// 第一次摆出模型时材质要编译着色器，编译完再爆炸、截图，不然截到的是默认灰材质
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
		{
			Goto(EStep::WaitModel, 1.f);
			break;
		}
		AToyExplodedModel* M = Model.Get();
		if (!M)
		{
			NextModel();
			break;
		}
		if (!M->IsExploded())
		{
			M->SetExploded(true);
		}
		Goto(EStep::WaitExplode, M->ExplodeDuration + 0.5f);
		break;
	}
	case EStep::WaitExplode:
	{
		Queue.Reset();
		if (AToyExplodedModel* M = Model.Get())
		{
			for (AToyPartUnit* Unit : M->GetUnits())
			{
				if (Unit && Unit->IsGrabbableUnit())
				{
					Queue.Add(Unit);
				}
			}
		}
		UnitIndex = -1;
		BeginUnit();
		break;
	}
	case EStep::Approach:
	{
		// 和桌面模式鼠标点中零件一样：手指沿视线水平方向、掌心朝下，捏合点放在零件上离镜头最近的表面点
		AToyPartUnit* Unit = Queue[UnitIndex].Get();
		if (!Unit || !Hand)
		{
			BeginUnit();
			break;
		}
		const FVector Eye = P->Camera->GetComponentLocation();
		const FVector Center = Unit->GetActorTransform().TransformPosition(Unit->GetLocalCenter());
		// 包围盒中心可能在孔里（阀门端盖、涵道）：瞄准包围盒里 3×3×3 个点，取离镜头最近的命中点（玩家伸手先碰到的那一面；
		// 取离中心最近的会穿过机身罩的通风孔抓到内壁上）
		const FBox Bounds = Unit->GetComponentsBoundingBox(false, true);
		FVector Point = Center;
		float Best = TNumericLimits<float>::Max();
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyGrabAudit), true);
		for (int32 I = 0; I < 27; ++I)
		{
			const FVector Aim = Center + Bounds.GetExtent() * 0.5f * FVector(I % 3 - 1, (I / 3) % 3 - 1, I / 9 - 1);
			for (UStaticMeshComponent* Mesh : Unit->GetMeshes())
			{
				FHitResult Hit;
				if (Mesh && Mesh->LineTraceComponent(Hit, Eye, Aim + (Aim - Eye).GetSafeNormal() * 50.f, Params)
					&& Hit.Distance < Best)
				{
					Best = Hit.Distance;
					Point = Hit.ImpactPoint;
				}
			}
		}
		const FVector Direction = (Point - Eye).GetSafeNormal();
		Hand->SetPreferredTarget(Unit);
		Hand->SetInputs(0.f, 0.f);
		Hand->SetDesktopPose(Point, FRotator(0.f, Direction.Rotation().Yaw, -90.f));
		Goto(EStep::Close, 0.25f);
		break;
	}
	case EStep::Close:
	{
		Hand->SetInputs(0.f, 1.f);   // 扳机 = 捏
		Goto(EStep::Present, 0.35f);
		break;
	}
	case EStep::Present:
	{
		AToyPartUnit* Unit = Queue[UnitIndex].Get();
		const bool bHeld = Unit && Hand && Hand->GetHeldActor() == Unit;
		// 抓住的这一刻量一下：手掌（抓点）和捏合点离零件真实表面多远。贴面 / 握圆柱 / 贴表面看手掌，捏看捏合点
		const float PalmGap = bHeld ? SurfaceGap(Hand->GripPoint->GetComponentLocation()) : -1.f;
		const float PinchGap = bHeld ? SurfaceGap(Hand->GetPinchLocation()) : -1.f;
		FText Title, Body;
		if (Unit)
		{
			Unit->GetGrabInfo(Title, Body);
		}
		const FString Kind = AToyStudio::KindName(Kinds[KindIndex]);
		const FString File = FString::Printf(TEXT("%02d_%s.png"), UnitIndex + 1, Unit ? *Unit->GetUnitId() : TEXT("missing"));
		CurrentRow = FString::Printf(TEXT("%s,%d,%s,%s,%s,%d,%.2f,%.2f,%s"), *Kind, UnitIndex + 1,
			Unit ? *Unit->GetUnitId() : TEXT(""), *CsvField(Title.ToString()), *CsvField(Unit ? Unit->DescribeGrab() : FString()),
			bHeld ? 1 : 0, PalmGap, PinchGap, *File);
		if (!bHeld)
		{
			++Missed;
			UE_LOG(LogTemp, Warning, TEXT("ToyBox: GrabAudit %s 没抓到"), Unit ? *Unit->GetUnitId() : TEXT("?"));
			Rows.Add(CurrentRow);
			Hand->SetInputs(0.f, 0.f);
			Goto(EStep::Release, 0.2f);
			break;
		}
		++Grabbed;
		PresentHeld();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(7301, 3.f, FColor::Yellow, FString::Printf(TEXT("%s %d/%d  %s  %s  palm %.1fcm pinch %.1fcm"),
				*Kind, UnitIndex + 1, Queue.Num(), *Unit->GetUnitId(), *Unit->DescribeGrab(), PalmGap, PinchGap));
		}
		Goto(EStep::Shoot, 0.45f);
		break;
	}
	case EStep::Shoot:
	{
		const FString File = FPaths::ConvertRelativePathToFull(OutDir / FString::Printf(TEXT("%02d_%s.png"), UnitIndex + 1,
			Queue[UnitIndex].IsValid() ? *Queue[UnitIndex]->GetUnitId() : TEXT("missing")));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		Rows.Add(CurrentRow);
		Goto(EStep::Release, 0.35f);
		break;
	}
	case EStep::Release:
	{
		if (Hand)
		{
			Hand->SetInputs(0.f, 0.f);
			Hand->SetPreferredTarget(nullptr);
		}
		if (P)
		{
			P->Camera->SetRelativeTransform(CameraHome);
		}
		// 等零件飞回原位（0.4 秒）再抓下一个
		Step = EStep::Approach;
		BeginUnit();
		Wait = 0.6f;
		break;
	}
	case EStep::Finish:
	default:
		break;
	}
}

void AToyGrabAudit::BeginUnit()
{
	++UnitIndex;
	if (!Queue.IsValidIndex(UnitIndex))
	{
		NextModel();
		return;
	}
	Goto(EStep::Approach);
}

void AToyGrabAudit::PresentHeld()
{
	// 像玩家抓起来那样把手往上、往镜头这边提一点，离开模型；然后镜头从手的前方给手和零件一个特写
	AToyBoxPawn* P = Pawn.Get();
	AToyHand* Hand = P->GetRightHand();
	AToyPartUnit* Unit = Queue[UnitIndex].Get();
	const FTransform View = P->Camera->GetComponentTransform();
	const FVector Flat = FVector(View.GetUnitAxis(EAxis::X).X, View.GetUnitAxis(EAxis::X).Y, 0.f).GetSafeNormal();
	Hand->SetActorLocation(Hand->GetActorLocation() + FVector(0.f, 0.f, 30.f) - Flat * 10.f);

	const FBox Bounds = Unit->GetComponentsBoundingBox(false, true);
	const float Size = static_cast<float>(Bounds.GetExtent().GetMax()) * 2.f;
	const FVector Focus = FMath::Lerp(Hand->GripPoint->GetComponentLocation(), Bounds.GetCenter(), 0.65f);
	// 手指沿 Flat 朝前伸、掌心朝下：镜头放在手的右前方、稍低一点往上看，看得到指尖、掌心和手里的零件（从上面看只看得到手背）
	const FVector LookDir = FRotator(12.f, Flat.Rotation().Yaw + 180.f - 50.f, 0.f).Vector();
	const float Distance = FMath::Clamp(Size * 1.3f + 22.f, 26.f, 70.f);
	P->Camera->SetWorldLocationAndRotation(Focus - LookDir * Distance, LookDir.Rotation());
}


float AToyGrabAudit::SurfaceGap(const FVector& From) const
{
	// 往 26 个方向各打 8cm，取最近的命中。离零件真实表面超过几厘米 = 手抓在空气里
	const AToyPartUnit* Unit = Queue.IsValidIndex(UnitIndex) ? Queue[UnitIndex].Get() : nullptr;
	if (!Unit)
	{
		return -1.f;
	}
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ToyGrabAuditGap), true);
	float Best = -1.f;
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				const FVector Dir = FVector(X, Y, Z).GetSafeNormal();
				if (Dir.IsNearlyZero())
				{
					continue;
				}
				for (UStaticMeshComponent* Mesh : Unit->GetMeshes())
				{
					FHitResult Hit;
					if (Mesh && Mesh->LineTraceComponent(Hit, From, From + Dir * 8.f, Params) && (Best < 0.f || Hit.Distance < Best))
					{
						Best = Hit.Distance;
					}
				}
			}
		}
	}
	return Best;
}

void AToyGrabAudit::NextModel()
{
	++KindIndex;
	if (!Kinds.IsValidIndex(KindIndex))
	{
		Finish();
		return;
	}
	Goto(EStep::ShowModel, 0.2f);
}

void AToyGrabAudit::Finish()
{
	Step = EStep::Finish;
	WriteSummary();
	UE_LOG(LogTemp, Log, TEXT("ToyBox: GrabAudit 完成：抓到 %d 个，没抓到 %d 个，截图在 %s"), Grabbed, Missed,
		*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GrabAudit")));
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

void AToyGrabAudit::WriteSummary()
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("GrabAudit") / TEXT("summary.csv");
	FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")) + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8);
}
