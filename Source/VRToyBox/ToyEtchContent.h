#pragma once

#include "CoreMinimal.h"
#include "ToyEtchCore.h"

/**
 * 干法刻蚀机的讲解文字，从 Content/Etcher/etcher_content.json 读（ITBasics 干法刻蚀机/tools/export_core.mjs 从网页版 etch_core.js 导出，
 * import_etcher.bat 拷过来）。网页和 VR 用同一份文字：15 步的"现在在干什么 / 为什么"、配方参数、预置配方、5 项判定、曲线和粒子的名字。
 */
struct FToyEtchStepText
{
	FString Name;
	FString Now;
	/** 主刻超时的时候过刻那一步换成这句（其他步是空的） */
	FString NowTimeout;
	FString Why;
};

struct FToyEtchField
{
	/** 配方里的键名：P / Ws / Wb / cl2 / hbr / o2 / T / oe */
	FString Key;
	FString Name;
	FString Unit;
	FString Tip;
	double Min = 0;
	double Max = 100;
	double Step = 1;
};

struct FToyEtchPreset
{
	FString Id;
	FString Name;
	ToyEtch::FRecipe R;
};

struct FToyEtchResultText
{
	FString Label;
	FString Hint;
	FString HintTimeout;
};

struct FToyEtchSeriesText
{
	FString Key;
	FString Name;
	FString Unit;
};

struct FToyEtchContent
{
	bool bLoaded = false;
	TArray<FToyEtchStepText> Steps;
	TArray<FToyEtchField> Fields;
	TArray<FToyEtchPreset> Presets;
	TArray<FToyEtchResultText> Results;
	TArray<FToyEtchSeriesText> Series;
	/** lowpower / lowflow / nogas → 原因 */
	TMap<FString, FString> Problems;
	/** gas / e / ion / rad / prod → 名字 */
	TMap<FString, FString> Particles;
	/** 待机时讲解板上的三行：标题、现在、提示 */
	FString ReadyTitle, ReadyNow, ReadyWhy;
	FString BadTitle, BadNow;
	FString VerdictOk, VerdictBad;

	/** 读不到文件时 bLoaded = false，各处用代码里的兜底文字 */
	bool Load(const FString& Path);

	/** 网页版 fillText()：把 {名字} 换成 Values 里的值，没有的原样留着 */
	static FString Fill(const FString& Text, const TMap<FString, FString>& Values);

	/** 配方里键名对应的那个数 */
	static double& RecipeValue(ToyEtch::FRecipe& R, const FString& Key);
	static double RecipeValue(const ToyEtch::FRecipe& R, const FString& Key);
};
