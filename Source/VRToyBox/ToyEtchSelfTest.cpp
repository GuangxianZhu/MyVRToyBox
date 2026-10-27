#include "ToyEtchContent.h"
#include "ToyEtchCore.h"
#include "ToyEtchProcess.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 调试命令 toy.EtchSelfTest [quit]：拿网页版算出来的标准答案（Content/Etcher/etcher_golden.json）逐组对 C++ 版工艺模型，
// 顺便检查讲解文字（etcher_content.json）读得进来。两个文件都是 ITBasics 干法刻蚀机/tools/export_core.mjs 生成的。
// 第二部分：5 个预置配方各按 15 步整套工艺（ToyEtch::FProcess，演示模式跑的就是它）从晶圆盒跑回晶圆盒，判定要和标准答案一模一样。
namespace
{
	TSharedPtr<FJsonObject> LoadEtcherJson(const TCHAR* FileName)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Etcher") / FileName;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: EtchSelfTest 读不到 %s"), *Path);
			return nullptr;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: EtchSelfTest %s 不是合法的 JSON"), *Path);
			return nullptr;
		}
		return Json;
	}

	bool EtchNearlyEqual(double A, double B, double Tolerance)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	/** 一个要对的量：json 里的键名 + C++ 算出来的值 */
	struct FEtchCheck
	{
		const TCHAR* Key;
		double Value;
	};

	// 返回不一致的项（空 = 这一组全对）
	TArray<FString> CompareEtchCase(const FJsonObject& Case)
	{
		TArray<FString> Errors;
		const TSharedPtr<FJsonObject>& In = Case.GetObjectField(TEXT("r"));
		ToyEtch::FRecipe R;
		R.P = In->GetNumberField(TEXT("P"));
		R.Ws = In->GetNumberField(TEXT("Ws"));
		R.Wb = In->GetNumberField(TEXT("Wb"));
		R.Cl2 = In->GetNumberField(TEXT("cl2"));
		R.HBr = In->GetNumberField(TEXT("hbr"));
		R.O2 = In->GetNumberField(TEXT("o2"));
		R.T = In->GetNumberField(TEXT("T"));
		R.Oe = In->GetNumberField(TEXT("oe"));
		const ToyEtch::FSimulation Sim = ToyEtch::SimulateEtch(R);

		const FString Problem = UTF8_TO_TCHAR(ToyEtch::ProblemKey(Sim.M.Problem));
		if (Problem != Case.GetStringField(TEXT("problemCode")))
		{
			Errors.Add(FString::Printf(TEXT("problemCode %s"), *Problem));
		}
		// 稳态量：只差浮点舍入
		const TSharedPtr<FJsonObject>& WantModel = Case.GetObjectField(TEXT("model"));
		const FEtchCheck ModelValues[] = {
			{ TEXT("n"), Sim.M.n }, { TEXT("Ei"), Sim.M.Ei }, { TEXT("Gn"), Sim.M.Gn }, { TEXT("ERv"), Sim.M.ERv },
			{ TEXT("ERl"), Sim.M.ERl }, { TEXT("ERox"), Sim.M.ERox }, { TEXT("ERmask"), Sim.M.ERmask }, { TEXT("u"), Sim.M.u },
			{ TEXT("pass"), Sim.M.pass }, { TEXT("sel"), Sim.M.sel }, { TEXT("total"), Sim.M.total },
		};
		for (const FEtchCheck& Value : ModelValues)
		{
			const double Want = WantModel->GetNumberField(Value.Key);
			if (!EtchNearlyEqual(Value.Value, Want, 1e-6 * FMath::Max(1.0, FMath::Abs(Want))))
			{
				Errors.Add(FString::Printf(TEXT("%s %.6g（应为 %.6g）"), Value.Key, Value.Value, Want));
			}
		}
		const TSharedPtr<FJsonObject>* WantEtch = nullptr;
		if (!Case.TryGetObjectField(TEXT("etch"), WantEtch))
		{
			return Errors;   // 点不着的配方没有刻蚀过程
		}
		// 刻蚀过程：终点是按 0.05 秒一步判的，浮点舍入最多让它差一步，所以时间放宽到一步
		const ToyEtch::FEtch& E = Sim.E;
		const FEtchCheck EtchValues[] = {
			{ TEXT("Dc"), E.Dc }, { TEXT("De"), E.De }, { TEXT("maskH"), E.MaskH }, { TEXT("ox"), E.Ox }, { TEXT("si"), E.Si },
			{ TEXT("gateLoss"), E.GateLoss }, { TEXT("tMain"), E.tMain }, { TEXT("uncleared"), E.Uncleared }, { TEXT("oesF"), E.OesF },
			{ TEXT("rmax"), Sim.Result.SideCutMax },
		};
		for (const FEtchCheck& Value : EtchValues)
		{
			const double Want = (*WantEtch)->GetNumberField(Value.Key);
			const double Tolerance = FCString::Strcmp(Value.Key, TEXT("tMain")) == 0 ? 0.051 : 1e-4 * FMath::Max(1.0, FMath::Abs(Want));
			if (!EtchNearlyEqual(Value.Value, Want, Tolerance))
			{
				Errors.Add(FString::Printf(TEXT("%s %.6g（应为 %.6g）"), Value.Key, Value.Value, Want));
			}
		}
		if (E.bTimeout != (*WantEtch)->GetBoolField(TEXT("timeout")))
		{
			Errors.Add(TEXT("timeout"));
		}
		const TArray<TSharedPtr<FJsonValue>>& WantOk = Case.GetArrayField(TEXT("ok"));
		for (int32 i = 0; i < 5 && i < WantOk.Num(); i++)
		{
			if (Sim.Result.Ok[i] != WantOk[i]->AsBool())
			{
				Errors.Add(FString::Printf(TEXT("第 %d 项判定"), i + 1));
			}
		}
		return Errors;
	}

	/**
	 * 整套工艺跑一片，和只算刻蚀那一段的标准答案比。判定（5 项合格 / 不合格）必须一样；数值允许差一点：
	 * 网页里主刻 → 过刻 → 关射频是三步，步和步之间隔一帧（0.05 秒）还在刻，标准答案没有这两帧，
	 * 所以刻穿以后挖进硅的深度这类量会多出零点几 nm（偏压太高那组 7.5 对 6.8），主刻时间晚一两帧。
	 */
	TArray<FString> CompareWholeRun(const FToyEtchPreset& Preset, const FJsonObject& Case)
	{
		TArray<FString> Errors;
		ToyEtch::FProcess Process;
		if (!ToyEtch::RunWholeProcess(Process, Preset.R))
		{
			Errors.Add(FString::Printf(TEXT("没跑完（停在第 %d 步，工艺时间 %.0f 秒）"), Process.StepIdx + 1, Process.SimT));
			return Errors;
		}
		const ToyEtch::FEtch& E = Process.Result.E;
		const TSharedPtr<FJsonObject>* WantEtch = nullptr;
		if (Case.TryGetObjectField(TEXT("etch"), WantEtch))
		{
			const FEtchCheck EtchValues[] = {
				{ TEXT("Dc"), E.Dc }, { TEXT("De"), E.De }, { TEXT("maskH"), E.MaskH }, { TEXT("ox"), E.Ox }, { TEXT("si"), E.Si },
				{ TEXT("gateLoss"), E.GateLoss }, { TEXT("tMain"), E.tMain }, { TEXT("uncleared"), E.Uncleared },
				{ TEXT("rmax"), Process.Result.V.SideCutMax },
			};
			for (const FEtchCheck& Value : EtchValues)
			{
				const double Want = (*WantEtch)->GetNumberField(Value.Key);
				double Tolerance = FMath::Max(1.0, 0.03 * FMath::Abs(Want));
				if (FCString::Strcmp(Value.Key, TEXT("tMain")) == 0)
				{
					Tolerance = 0.151;
				}
				else if (FCString::Strcmp(Value.Key, TEXT("uncleared")) == 0)
				{
					Tolerance = 0.2;
				}
				if (!EtchNearlyEqual(Value.Value, Want, Tolerance))
				{
					Errors.Add(FString::Printf(TEXT("%s %.4g（应为 %.4g）"), Value.Key, Value.Value, Want));
				}
			}
			if (E.bTimeout != (*WantEtch)->GetBoolField(TEXT("timeout")))
			{
				Errors.Add(TEXT("timeout"));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>& WantOk = Case.GetArrayField(TEXT("ok"));
		for (int32 i = 0; i < 5 && i < WantOk.Num(); i++)
		{
			if (Process.Result.V.Ok[i] != WantOk[i]->AsBool())
			{
				Errors.Add(FString::Printf(TEXT("第 %d 项判定"), i + 1));
			}
		}
		FString Marks;
		for (const bool bOk : Process.Result.V.Ok)
		{
			Marks += bOk ? TEXT("√") : TEXT("×");
		}
		UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchSelfTest 整套工艺 %s：%.1f 秒跑完，主刻 %.2f s，判定 %s"), *Preset.Id, Process.SimT, E.tMain, *Marks);
		return Errors;
	}

	void RunEtchSelfTest(const TArray<FString>& Args, UWorld* World)
	{
		int32 Passed = 0, Total = 0;
		int32 RunPassed = 0, RunTotal = 0;
		const TSharedPtr<FJsonObject> Golden = LoadEtcherJson(TEXT("etcher_golden.json"));
		if (Golden)
		{
			for (const TSharedPtr<FJsonValue>& Value : Golden->GetArrayField(TEXT("cases")))
			{
				const TSharedPtr<FJsonObject>& Case = Value->AsObject();
				const TArray<FString> Errors = CompareEtchCase(*Case);
				Total++;
				if (Errors.Num() == 0)
				{
					Passed++;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("ToyBox: EtchSelfTest %s 不一致：%s"), *Case->GetStringField(TEXT("id")), *FString::Join(Errors, TEXT("，")));
				}
			}
		}
		if (const TSharedPtr<FJsonObject> Content = LoadEtcherJson(TEXT("etcher_content.json")))
		{
			UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchSelfTest 讲解文字 %d 步、%d 个零件、%d 个预置配方、%d 个配方参数"),
				Content->GetArrayField(TEXT("steps")).Num(), Content->GetArrayField(TEXT("parts")).Num(),
				Content->GetArrayField(TEXT("presets")).Num(), Content->GetArrayField(TEXT("fields")).Num());
		}

		// 第二部分：预置配方跑整套工艺（标准答案里同名的那一组）
		FToyEtchContent Texts;
		if (Golden && Texts.Load(FPaths::ProjectContentDir() / TEXT("Etcher/etcher_content.json")))
		{
			for (const FToyEtchPreset& Preset : Texts.Presets)
			{
				const TSharedPtr<FJsonValue>* Found = Golden->GetArrayField(TEXT("cases")).FindByPredicate([&Preset](const TSharedPtr<FJsonValue>& Value)
				{
					return Value->AsObject()->GetStringField(TEXT("id")) == Preset.Id;
				});
				if (!Found)
				{
					UE_LOG(LogTemp, Warning, TEXT("ToyBox: EtchSelfTest 标准答案里没有预置配方 %s"), *Preset.Id);
					continue;
				}
				const TArray<FString> Errors = CompareWholeRun(Preset, *(*Found)->AsObject());
				RunTotal++;
				if (Errors.Num() == 0)
				{
					RunPassed++;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("ToyBox: EtchSelfTest 整套工艺 %s 不一致：%s"), *Preset.Id, *FString::Join(Errors, TEXT("，")));
				}
			}
		}

		if (Total > 0 && Passed == Total && RunTotal > 0 && RunPassed == RunTotal)
		{
			UE_LOG(LogTemp, Log, TEXT("ToyBox: EtchSelfTest %d/%d 组和网页版一致；整套工艺 %d/%d 个预置配方判定一致"), Passed, Total, RunPassed, RunTotal);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ToyBox: EtchSelfTest 只有 %d/%d 组和网页版一致；整套工艺 %d/%d 个预置配方判定一致"), Passed, Total, RunPassed, RunTotal);
		}
		if (World && Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); }))
		{
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EtchSelfTestCommand(
		TEXT("toy.EtchSelfTest"),
		TEXT("拿网页版的标准答案（Content/Etcher/etcher_golden.json）逐组对 C++ 版刻蚀工艺模型。参数 quit = 跑完退出"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEtchSelfTest));
}
