#include "ToyEtchContent.h"

#include "ToyEtchProcess.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString Str(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	double Num(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Fallback)
	{
		double Value = Fallback;
		Object->TryGetNumberField(Field, Value);
		return Value;
	}

	/** explain.ready 这种 ["标题", "现在", "提示"] 数组的第 Index 项 */
	FString Line(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Index)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->IsValidIndex(Index))
		{
			return (*Values)[Index]->AsString();
		}
		return FString();
	}

	ToyEtch::FRecipe ReadRecipe(const TSharedPtr<FJsonObject>& Object)
	{
		ToyEtch::FRecipe R;
		for (const TCHAR* Key : { TEXT("P"), TEXT("Ws"), TEXT("Wb"), TEXT("cl2"), TEXT("hbr"), TEXT("o2"), TEXT("T"), TEXT("oe") })
		{
			double& Value = FToyEtchContent::RecipeValue(R, Key);
			Value = Num(Object, Key, Value);
		}
		return R;
	}
}

double& FToyEtchContent::RecipeValue(ToyEtch::FRecipe& R, const FString& Key)
{
	if (Key == TEXT("P")) return R.P;
	if (Key == TEXT("Ws")) return R.Ws;
	if (Key == TEXT("Wb")) return R.Wb;
	if (Key == TEXT("cl2")) return R.Cl2;
	if (Key == TEXT("hbr")) return R.HBr;
	if (Key == TEXT("o2")) return R.O2;
	if (Key == TEXT("T")) return R.T;
	return R.Oe;
}

double FToyEtchContent::RecipeValue(const ToyEtch::FRecipe& R, const FString& Key)
{
	return RecipeValue(const_cast<ToyEtch::FRecipe&>(R), Key);
}

FString FToyEtchContent::Fill(const FString& Text, const TMap<FString, FString>& Values)
{
	FString Out;
	Out.Reserve(Text.Len());
	int32 Index = 0;
	while (Index < Text.Len())
	{
		int32 Close = INDEX_NONE;
		if (Text[Index] == TEXT('{'))
		{
			Close = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
		}
		if (Close != INDEX_NONE)
		{
			const FString Key = Text.Mid(Index + 1, Close - Index - 1);
			if (const FString* Value = Values.Find(Key))
			{
				Out += *Value;
				Index = Close + 1;
				continue;
			}
		}
		Out.AppendChar(Text[Index]);
		++Index;
	}
	return Out;
}

bool FToyEtchContent::Load(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: 读不到刻蚀机讲解文字 %s（先运行 导入刻蚀机模型.bat）"), *Path);
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: %s 不是合法的 JSON"), *Path);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Json->TryGetArrayField(TEXT("steps"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Steps.Add(FToyEtchStepText{ Str(Object, TEXT("name")), Str(Object, TEXT("now")), Str(Object, TEXT("nowTimeout")), Str(Object, TEXT("why")) });
		}
	}
	if (Json->TryGetArrayField(TEXT("fields"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FToyEtchField& Field = Fields.AddDefaulted_GetRef();
			Field.Key = Str(Object, TEXT("k"));
			Field.Name = Str(Object, TEXT("name"));
			Field.Unit = Str(Object, TEXT("unit"));
			Field.Tip = Str(Object, TEXT("tip"));
			Field.Min = Num(Object, TEXT("min"), 0);
			Field.Max = Num(Object, TEXT("max"), 100);
			Field.Step = FMath::Max(1e-3, Num(Object, TEXT("step"), 1));
		}
	}
	if (Json->TryGetArrayField(TEXT("presets"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TSharedPtr<FJsonObject>* RecipeObject = nullptr;
			FToyEtchPreset& Preset = Presets.AddDefaulted_GetRef();
			Preset.Id = Str(Object, TEXT("id"));
			Preset.Name = Str(Object, TEXT("name"));
			if (Object->TryGetObjectField(TEXT("r"), RecipeObject))
			{
				Preset.R = ReadRecipe(*RecipeObject);
			}
		}
	}
	if (Json->TryGetArrayField(TEXT("results"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Results.Add(FToyEtchResultText{ Str(Object, TEXT("label")), Str(Object, TEXT("hint")), Str(Object, TEXT("hintTimeout")) });
		}
	}
	if (Json->TryGetArrayField(TEXT("series"), Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			Series.Add(FToyEtchSeriesText{ Str(Object, TEXT("k")), Str(Object, TEXT("name")), Str(Object, TEXT("unit")) });
		}
	}
	for (const TCHAR* MapField : { TEXT("problems"), TEXT("particles") })
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Json->TryGetObjectField(MapField, Object))
		{
			TMap<FString, FString>& Target = FCString::Strcmp(MapField, TEXT("problems")) == 0 ? Problems : Particles;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
			{
				Target.Add(Pair.Key, Pair.Value->AsString());
			}
		}
	}
	const TSharedPtr<FJsonObject>* Explain = nullptr;
	if (Json->TryGetObjectField(TEXT("explain"), Explain))
	{
		ReadyTitle = Line(*Explain, TEXT("ready"), 0);
		ReadyNow = Line(*Explain, TEXT("ready"), 1);
		ReadyWhy = Line(*Explain, TEXT("ready"), 2);
		BadTitle = Line(*Explain, TEXT("badRecipe"), 0);
		BadNow = Line(*Explain, TEXT("badRecipe"), 1);
	}
	const TSharedPtr<FJsonObject>* Verdict = nullptr;
	if (Json->TryGetObjectField(TEXT("verdict"), Verdict))
	{
		VerdictOk = Str(*Verdict, TEXT("ok"));
		VerdictBad = Str(*Verdict, TEXT("bad"));
	}

	bLoaded = Steps.Num() == ToyEtch::StepCount && Fields.Num() > 0 && Results.Num() == 5;
	if (!bLoaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToyBox: %s 内容不全（%d 步、%d 个参数、%d 项判定）"), *Path, Steps.Num(), Fields.Num(), Results.Num());
	}
	return bLoaded;
}
