#include "ToyBoxPawn.h"
#include "ToyEtcherRig.h"
#include "ToyExplodedModel.h"
#include "ToyMenuPanel.h"
#include "ToyStudio.h"
#include "ToyTouchScreen.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

// 调试命令 toy.EtchShots [quit]：桌面模式下把刻蚀机演示模式自动走一遍并截图到 Saved/EtchShots/（给没戴头显时检查用）。
//   摆出刻蚀机 → 待机全景 → 标准配方 10 倍速自动跑：真空机械手送片时拍小模型，主刻蚀到一半暂停，拍全景、放大腔、小模型辉光、
//   讲解板、剖面板、触屏流程页 → 跑完拍结果板和触屏的结果 / 配方 / 结构页 → 再用"偏压太高"跑一片拍不合格的结果 →
//   拆开拍一张、按开始拍"先组装回去"，再装回去。镜头直接摆，拍完恢复；带 quit 跑完退出游戏。
namespace
{
	class FEtchShotRunner : public TSharedFromThis<FEtchShotRunner>
	{
	public:
		/** 一步：每帧调用，参数是这一步已经过了多少秒，返回 true 进下一步 */
		using FStep = TFunction<bool(float)>;

		FEtchShotRunner(UWorld* InWorld, bool bInQuit) : World(InWorld), bQuit(bInQuit)
		{
			OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EtchShots"));
			IFileManager::Get().MakeDirectory(*OutDir, true);
			BuildSteps();
		}

		void Start()
		{
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FEtchShotRunner::Tick));
			Self = AsShared();
			UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchShots 开始，截图存到 %s"), *OutDir);
		}

	private:
		bool Tick(float DeltaSeconds)
		{
			if (!World.IsValid())
			{
				Stop();
				return false;
			}
			Elapsed += DeltaSeconds;
			if (Steps.IsValidIndex(StepIndex) && Steps[StepIndex](Elapsed))
			{
				++StepIndex;
				Elapsed = 0.f;
				bFlagA = bFlagB = false;
			}
			if (!Steps.IsValidIndex(StepIndex))
			{
				Finish();
				return false;
			}
			return true;
		}

		void Stop()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			Self.Reset();
		}

		void Finish()
		{
			if (AToyBoxPawn* P = Pawn.Get())
			{
				P->Camera->SetRelativeTransform(CameraOriginal);
				P->Camera->SetFieldOfView(FovOriginal);
				P->SetExternalControl(false);
			}
			UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchShots 完成，%d 张截图在 %s"), ShotCount, *OutDir);
			if (bQuit && World.IsValid())
			{
				UKismetSystemLibrary::QuitGame(World.Get(), nullptr, EQuitPreference::Quit, false);
			}
			Stop();
		}

		AToyEtcherRig* Rig() const
		{
			const AToyStudio* S = Studio.Get();
			return S ? S->GetEtcherRig() : nullptr;
		}

		void SetPaused(bool bPaused)
		{
			if (AToyEtcherRig* R = Rig(); R && R->IsPaused() != bPaused)
			{
				R->TogglePause();
			}
		}

		/** 镜头摆到 Eye 看 Target（世界坐标），视角 Fov 度 */
		void Aim(const FVector& Eye, const FVector& Target, float Fov)
		{
			if (AToyBoxPawn* P = Pawn.Get())
			{
				P->Camera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());
				P->Camera->SetFieldOfView(Fov);
			}
		}

		/** 模型支架（ModelAnchor）坐标 → 世界坐标：放大腔、面板都挂在它上面，挪过模型也对得上 */
		FVector AnchorPoint(const FVector& Local) const
		{
			const AToyStudio* S = Studio.Get();
			return S ? S->ModelAnchor->GetComponentTransform().TransformPosition(Local) : Local;
		}

		/** 正对着一块面板（WidgetComponent 正面是 +X）在 Distance 厘米外拍 */
		void AimAtBoard(const USceneComponent* Board, float Distance, float Fov)
		{
			if (Board)
			{
				const FVector Center = Board->GetComponentLocation();
				Aim(Center + Board->GetForwardVector() * Distance, Center, Fov);
			}
		}

		const USceneComponent* RigBoard(FName Name) const
		{
			AToyEtcherRig* R = Rig();
			if (!R)
			{
				return nullptr;
			}
			TInlineComponentArray<UWidgetComponent*> Boards(R);
			for (const UWidgetComponent* Board : Boards)
			{
				if (Board->GetFName() == Name)
				{
					return Board;
				}
			}
			return nullptr;
		}

		// ---------- 几种步骤 ----------
		FStep Do(TFunction<void()> Action)
		{
			return [Action](float) { Action(); return true; };
		}

		FStep Wait(float Seconds)
		{
			return [Seconds](float T) { return T >= Seconds; };
		}

		/** 等条件成立，最多等 Timeout 秒（超时记一条警告接着往下走） */
		FStep WaitUntil(const TCHAR* What, TFunction<bool()> Condition, float Timeout = 120.f)
		{
			return [What, Condition, Timeout](float T)
			{
				if (Condition())
				{
					return true;
				}
				if (T > Timeout)
				{
					UE_LOG(LogTemp, Warning, TEXT("ToyBox: EtchShots 等「%s」超时，接着往下"), What);
					return true;
				}
				return false;
			};
		}

		/** 摆镜头 → 等 2.5 秒让自动曝光追上新画面（近看暗面板后再拍全景会发白）→ 截图 → 等它写完 */
		FStep Shot(const TCHAR* Name, TFunction<void()> AimFn)
		{
			return [this, Name, AimFn](float T)
			{
				if (!bFlagA)
				{
					AimFn();
					bFlagA = true;
					return false;
				}
				if (!bFlagB && T > 2.5f)
				{
					FScreenshotRequest::RequestScreenshot(OutDir / FString(Name) + TEXT(".png"), false, false);
					bFlagB = true;
					++ShotCount;
					const AToyEtcherRig* R = Rig();
					UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchShots %s · %s"), Name, R ? *R->StatusLine().ToString() : TEXT("没有刻蚀机"));
					return false;
				}
				return bFlagB && T > 3.f;
			};
		}

		void BuildSteps()
		{
			// 等玩家和工作室，收起主菜单，摆出刻蚀机
			Steps.Add([this](float)
			{
				for (TActorIterator<AToyBoxPawn> It(World.Get()); It; ++It)
				{
					Pawn = *It;
				}
				for (TActorIterator<AToyStudio> It(World.Get()); It; ++It)
				{
					Studio = *It;
				}
				if (!Pawn.IsValid() || !Studio.IsValid())
				{
					return false;
				}
				Pawn->SetExternalControl(true);
				CameraOriginal = Pawn->Camera->GetRelativeTransform();
				FovOriginal = Pawn->Camera->FieldOfView;
				for (TActorIterator<AToyMenuPanel> It(World.Get()); It; ++It)
				{
					It->Close();
				}
				if (!Studio->IsModelShown(EToyModelKind::Etcher))
				{
					Studio->ToggleModel(EToyModelKind::Etcher);
				}
				return true;
			});
			Steps.Add(WaitUntil(TEXT("刻蚀机演示模式"), [this]() { return Rig() != nullptr; }, 5.f));
			// 第一次摆出来材质要编译着色器，编译完再拍
			Steps.Add(WaitUntil(TEXT("着色器编译"), []() { return !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0; }, 300.f));
			Steps.Add(Wait(1.f));

			auto Overview = [this]() { Aim(AnchorPoint(FVector(-110.f, 5.f, 105.f)), AnchorPoint(FVector(20.f, 5.f, 45.f)), 90.f); };
			auto Screen = [this]()
			{
				const AToyStudio* S = Studio.Get();
				const AToyTouchScreen* Touch = S ? S->GetScreen() : nullptr;
				AimAtBoard(Touch ? Touch->Gui.Get() : nullptr, 30.f, 65.f);
			};
			auto ScreenTab = [this, Screen](int32 Tab)
			{
				return [this, Screen, Tab]()
				{
					if (AToyStudio* S = Studio.Get(); S && S->GetScreen())
					{
						S->GetScreen()->SetEtchTab(Tab);
					}
					Screen();
				};
			};

			Steps.Add(Shot(TEXT("01_idle_overview"), Overview));
			Steps.Add(Shot(TEXT("02_screen_flow_idle"), ScreenTab(2)));

			// 标准配方、10 倍速、自动，开始
			Steps.Add(Do([this]()
			{
				if (AToyEtcherRig* R = Rig())
				{
					R->ApplyPreset(0);
					R->SetSpeedIndex(3);
					if (!R->IsAuto())
					{
						R->ToggleAuto();
					}
					R->StartOrNext();
				}
			}));
			// 第 5 步：真空机械手把晶圆送进工艺腔，伸到一半时拍小模型的闸阀那边
			Steps.Add(WaitUntil(TEXT("送片"), [this]()
			{
				const AToyEtcherRig* R = Rig();
				return R && R->GetProcess().StepIdx == 4 && R->GetProcess().S.bArm && R->GetProcess().S.Arm > 0.45;
			}));
			Steps.Add(Do([this]() { SetPaused(true); }));
			Steps.Add(Shot(TEXT("03_transfer_small"), [this]() { Aim(AnchorPoint(FVector(-58.f, 26.f, 58.f)), AnchorPoint(FVector(-12.f, 0.f, 36.f)), 70.f); }));
			Steps.Add(Shot(TEXT("04_transfer_chamber"), [this]() { Aim(AnchorPoint(FVector(-30.f, -22.f, 62.f)), AnchorPoint(FVector(10.f, -54.f, 40.f)), 70.f); }));
			Steps.Add(Do([this]() { SetPaused(false); }));

			// 第 9 步主刻蚀，刻到一半暂停：粒子、辉光、剖面、曲线都在
			Steps.Add(WaitUntil(TEXT("主刻蚀刻到一半"), [this]()
			{
				const AToyEtcherRig* R = Rig();
				return R && R->GetProcess().StepIdx == 8 && R->GetProcess().E.Dc > 45.0;
			}));
			Steps.Add(Do([this]() { SetPaused(true); }));
			Steps.Add(Wait(0.5f));
			Steps.Add(Shot(TEXT("05_etch_overview"), Overview));
			Steps.Add(Shot(TEXT("06_etch_chamber"), [this]() { Aim(AnchorPoint(FVector(-26.f, -24.f, 62.f)), AnchorPoint(FVector(14.f, -54.f, 42.f)), 70.f); }));
			Steps.Add(Shot(TEXT("07_etch_small_glow"), [this]() { Aim(AnchorPoint(FVector(-37.f, 5.f, 63.f)), AnchorPoint(FVector(0.f, 0.f, 36.f)), 70.f); }));
			Steps.Add(Shot(TEXT("08_info_board"), [this]() { AimAtBoard(RigBoard(TEXT("InfoBoard")), 60.f, 65.f); }));
			Steps.Add(Shot(TEXT("09_profile_board"), [this]() { AimAtBoard(RigBoard(TEXT("ProfileBoard")), 50.f, 55.f); }));
			Steps.Add(Shot(TEXT("10_screen_flow_etching"), ScreenTab(2)));
			Steps.Add(Do([this]() { SetPaused(false); }));

			// 跑完：结果板、触屏的结果 / 配方 / 结构页
			Steps.Add(WaitUntil(TEXT("标准配方跑完"), [this]() { const AToyEtcherRig* R = Rig(); return R && R->GetProcess().bDone; }));
			Steps.Add(Wait(0.5f));
			Steps.Add(Shot(TEXT("11_done_overview"), Overview));
			Steps.Add(Shot(TEXT("12_result_board"), [this]() { AimAtBoard(RigBoard(TEXT("ResultBoard")), 52.f, 55.f); }));
			Steps.Add(Shot(TEXT("13_screen_result"), ScreenTab(4)));
			Steps.Add(Shot(TEXT("14_screen_recipe"), ScreenTab(3)));
			Steps.Add(Shot(TEXT("15_screen_structure"), ScreenTab(1)));

			// 再跑一片"偏压太高"：栅氧被打穿、光刻胶不够，结果不合格
			Steps.Add(Do([this]()
			{
				if (AToyEtcherRig* R = Rig())
				{
					R->ResetProcess();
					R->ApplyPreset(2);
					R->StartOrNext();
				}
			}));
			Steps.Add(WaitUntil(TEXT("偏压太高跑完"), [this]() { const AToyEtcherRig* R = Rig(); return R && R->GetProcess().bDone; }));
			Steps.Add(Wait(0.5f));
			Steps.Add(Shot(TEXT("16_hibias_result_board"), [this]() { AimAtBoard(RigBoard(TEXT("ResultBoard")), 52.f, 55.f); }));
			Steps.Add(Shot(TEXT("17_hibias_profile"), [this]() { AimAtBoard(RigBoard(TEXT("ProfileBoard")), 50.f, 55.f); }));

			// 拆开：放大腔收起来；这时按开始，讲解板提示先组装回去
			Steps.Add(Do([this]()
			{
				if (AToyStudio* S = Studio.Get())
				{
					S->ToggleModelExploded();
				}
			}));
			Steps.Add(Wait(3.f));
			Steps.Add(Do([this]()
			{
				if (AToyEtcherRig* R = Rig())
				{
					R->StartOrNext();   // 跑完的那片先回待机
					R->StartOrNext();   // 拆开着，开始不了
				}
			}));
			Steps.Add(Shot(TEXT("18_exploded_overview"), Overview));
			Steps.Add(Shot(TEXT("19_need_assemble"), [this]() { AimAtBoard(RigBoard(TEXT("InfoBoard")), 60.f, 65.f); }));
			Steps.Add(Do([this]()
			{
				if (AToyStudio* S = Studio.Get())
				{
					S->ToggleModelExploded();
				}
				if (AToyEtcherRig* R = Rig(); R && R->IsAuto())
				{
					R->ToggleAuto();
				}
			}));
			Steps.Add(Wait(3.f));
			Steps.Add(Shot(TEXT("20_assembled_again"), Overview));
		}

		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		FString OutDir;
		TArray<FStep> Steps;
		int32 StepIndex = 0;
		float Elapsed = 0.f;
		bool bFlagA = false;
		bool bFlagB = false;
		int32 ShotCount = 0;
		TWeakObjectPtr<AToyBoxPawn> Pawn;
		TWeakObjectPtr<AToyStudio> Studio;
		FTransform CameraOriginal;
		float FovOriginal = 90.f;
		FTSTicker::FDelegateHandle Handle;
		/** 自己拉着自己，跑完 Stop() 放掉 */
		TSharedPtr<FEtchShotRunner> Self;
	};

	void StartEtchShots(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const bool bQuit = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		MakeShared<FEtchShotRunner>(World, bQuit)->Start();
	}

	FAutoConsoleCommandWithWorldAndArgs EtchShotsCommand(
		TEXT("toy.EtchShots"),
		TEXT("桌面模式下把刻蚀机演示模式自动走一遍并截图到 Saved/EtchShots/。参数 quit = 跑完退出"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartEtchShots));
}
