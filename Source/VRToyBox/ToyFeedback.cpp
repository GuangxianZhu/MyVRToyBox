#include "ToyFeedback.h"

#include "ToyBoxPawn.h"
#include "ToyGrabbable.h"
#include "ToySettings.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

static TAutoConsoleVariable<int32> CVarFeedbackLog(
	TEXT("toy.FeedbackLog"),
	0,
	TEXT("1 = 每个声音 / 震动事件打一行日志（桌面模式感觉不到震动时用来核对）"));

namespace
{
	const TCHAR* AudioDir = TEXT("/Game/Audio/");
	constexpr float SourceCooldown = 0.06f;

	const TCHAR* EventName(EToyFeedback Event)
	{
		static const TCHAR* Names[] = {
			TEXT("HoverEnter"), TEXT("ServoTick"), TEXT("FingerTouch"), TEXT("GrabSecured"), TEXT("GhostReady"), TEXT("ParkPulse"),
			TEXT("Release"), TEXT("ReturnWhoosh"), TEXT("MagnetSnap"), TEXT("Seat"), TEXT("UiHover"), TEXT("UiPress"), TEXT("UiRelease"),
			TEXT("MenuOpen"), TEXT("MenuClose") };
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EToyFeedback::Count), "事件名表要和 EToyFeedback 对齐");
		return Names[static_cast<int32>(Event)];
	}

	/** 同一来源的冷却只管会连发的事件（碰撞类）。指尖碰到每根手指只响一次、伺服齿轮和倒车雷达自己控制节奏，
	 * 不能被冷却吞掉：三根手指常常在 60ms 内先后碰到，同一只手发出的第二、三下会被当成重复 */
	bool UsesCooldown(EToyFeedback Event)
	{
		return Event != EToyFeedback::FingerTouch && Event != EToyFeedback::ServoTick && Event != EToyFeedback::ParkPulse;
	}

	/** 材料敲击声的文件名前缀（Tap_Heavy_1.wav …），抓取器自己按轻金属算。 */
	FString TapCue(EToyMaterial Material)
	{
		switch (Material)
		{
		case EToyMaterial::HeavyMetal: return TEXT("Tap_Heavy");
		case EToyMaterial::LightMetal:
		case EToyMaterial::Claw:       return TEXT("Tap_Light");
		case EToyMaterial::Rubber:     return TEXT("Tap_Rubber");
		case EToyMaterial::Brittle:    return TEXT("Tap_Brittle");
		default:                       return TEXT("Tap_Plastic");
		}
	}

	/** 材料"硬不硬"：敲击声音量、震动力度都乘它。 */
	float Hardness(EToyMaterial Material)
	{
		switch (Material)
		{
		case EToyMaterial::HeavyMetal: return 1.f;
		case EToyMaterial::LightMetal: return 0.85f;
		case EToyMaterial::Brittle:    return 0.9f;
		case EToyMaterial::Rubber:     return 0.45f;
		case EToyMaterial::Claw:       return 0.8f;
		default:                       return 0.7f;
		}
	}

	/** 震动的衰减时间（秒）：硬的干脆，橡胶软、拖得长一点。 */
	float HapticDecay(EToyMaterial Material)
	{
		switch (Material)
		{
		case EToyMaterial::HeavyMetal: return 0.03f;
		case EToyMaterial::LightMetal: return 0.025f;
		case EToyMaterial::Brittle:    return 0.015f;
		case EToyMaterial::Rubber:     return 0.05f;
		default:                       return 0.02f;
		}
	}
}

UToyFeedback* UToyFeedback::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UToyFeedback>() : nullptr;
}

EToyMaterial UToyFeedback::ClassifyMaterial(const FString& Text)
{
	// 文字里最先出现的关键词定材料（"HT250 铸铁机座 + 漆包铜线"→ 重金属）。
	// 不用单独的"硅"：硅钢片是金属，只认硅片 / 晶圆这类
	struct FKeyword { const TCHAR* Word; EToyMaterial Material; };
	static const FKeyword Keywords[] = {
		{ TEXT("钢"), EToyMaterial::HeavyMetal }, { TEXT("铁"), EToyMaterial::HeavyMetal }, { TEXT("铜"), EToyMaterial::HeavyMetal },
		{ TEXT("钨"), EToyMaterial::HeavyMetal }, { TEXT("锌"), EToyMaterial::HeavyMetal }, { TEXT("镍"), EToyMaterial::HeavyMetal },
		{ TEXT("铝"), EToyMaterial::LightMetal }, { TEXT("镁"), EToyMaterial::LightMetal }, { TEXT("钛"), EToyMaterial::LightMetal },
		{ TEXT("塑料"), EToyMaterial::HardPlastic }, { TEXT("尼龙"), EToyMaterial::HardPlastic }, { TEXT("PC"), EToyMaterial::HardPlastic },
		{ TEXT("ABS"), EToyMaterial::HardPlastic }, { TEXT("碳纤维"), EToyMaterial::HardPlastic }, { TEXT("TPU"), EToyMaterial::HardPlastic },
		{ TEXT("POM"), EToyMaterial::HardPlastic }, { TEXT("树脂"), EToyMaterial::HardPlastic }, { TEXT("环氧"), EToyMaterial::HardPlastic },
		{ TEXT("FR4"), EToyMaterial::HardPlastic }, { TEXT("FR-4"), EToyMaterial::HardPlastic },
		{ TEXT("橡胶"), EToyMaterial::Rubber }, { TEXT("硅胶"), EToyMaterial::Rubber }, { TEXT("TPE"), EToyMaterial::Rubber },
		{ TEXT("玻璃"), EToyMaterial::Brittle }, { TEXT("陶瓷"), EToyMaterial::Brittle }, { TEXT("硅片"), EToyMaterial::Brittle },
		{ TEXT("晶圆"), EToyMaterial::Brittle }, { TEXT("单晶硅"), EToyMaterial::Brittle }, { TEXT("多晶硅"), EToyMaterial::Brittle },
		{ TEXT("石英"), EToyMaterial::Brittle }, { TEXT("蓝宝石"), EToyMaterial::Brittle },
	};
	int32 BestIndex = MAX_int32;
	EToyMaterial Best = EToyMaterial::HardPlastic;
	for (const FKeyword& Keyword : Keywords)
	{
		const int32 Index = Text.Find(Keyword.Word, ESearchCase::IgnoreCase);
		if (Index != INDEX_NONE && Index < BestIndex)
		{
			BestIndex = Index;
			Best = Keyword.Material;
		}
	}
	return Best;
}

EToyMaterial UToyFeedback::MaterialOf(const AActor* Actor)
{
	const IToyGrabbable* Grabbable = Cast<IToyGrabbable>(Actor);
	return Grabbable ? ClassifyMaterial(Grabbable->GetMaterialText()) : EToyMaterial::HardPlastic;
}

float UToyFeedback::SizeOf(const AActor* Actor)
{
	if (!Actor)
	{
		return 1000.f;
	}
	const FVector Size = Actor->GetComponentsBoundingBox(false, true).GetSize();
	return FMath::Max(1.f, static_cast<float>(Size.X * Size.Y * Size.Z));
}

const TCHAR* UToyFeedback::MaterialName(EToyMaterial Material)
{
	static const TCHAR* Names[] = { TEXT("重金属"), TEXT("轻金属"), TEXT("硬塑料"), TEXT("橡胶"), TEXT("脆性"), TEXT("抓取器") };
	return Names[static_cast<int32>(Material)];
}

void UToyFeedback::EnsureSettings()
{
	if (!Attenuation)
	{
		// 3D 定位：60cm 以内不衰减，往外 8m 衰减到 0（桌边玩，主要是听出左右手、远近）
		Attenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(60.f, 0.f, 0.f);
		Settings.FalloffDistance = 800.f;
	}
	if (!Concurrency)
	{
		Concurrency = NewObject<USoundConcurrency>(this);
		Concurrency->Concurrency.MaxCount = 12;
		Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
	}
}

USoundBase* UToyFeedback::PickVariant(const FString& Cue)
{
	FCue* Found = Cues.Find(Cue);
	if (!Found)
	{
		// 第一次用到才加载：Cue_1、Cue_2 … 一直到找不到；没有编号的就是单个文件（循环声、菜单声）
		FCue& New = Cues.Add(Cue);
		for (int32 K = 1; K <= 8; ++K)
		{
			const FString Name = FString::Printf(TEXT("%s_%d"), *Cue, K);
			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *(AudioDir + Name + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Sound)
			{
				break;
			}
			New.Variants.Add(Sound);
			Loaded.Add(Sound);
		}
		if (New.Variants.Num() == 0)
		{
			if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *(AudioDir + Cue + TEXT(".") + Cue), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				New.Variants.Add(Sound);
				Loaded.Add(Sound);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ToyBox: 找不到音效 %s（跑 导入音效.bat）"), *Cue);
			}
		}
		Found = &New;
	}
	const int32 Count = Found->Variants.Num();
	if (Count == 0)
	{
		return nullptr;
	}
	// 随机挑一个，但不和上一次一样
	int32 Index = FMath::RandRange(0, Count - 1);
	if (Count > 1 && Index == Found->Last)
	{
		Index = (Index + 1 + FMath::RandRange(0, Count - 2)) % Count;
	}
	Found->Last = Index;
	return Found->Variants[Index];
}

void UToyFeedback::PlaySound(const FString& Cue, const FVector& Location, float Volume, float Pitch)
{
	const float Gain = ToySettings::SoundGain();
	if (Gain <= 0.f || Volume <= 0.f)
	{
		return;
	}
	USoundBase* Sound = PickVariant(Cue);
	if (!Sound)
	{
		return;
	}
	EnsureSettings();
	const float Db = FMath::FRandRange(-2.f, 2.f);
	const float FinalVolume = Volume * Gain * FMath::Pow(10.f, Db / 20.f);
	const float FinalPitch = FMath::Clamp(Pitch * FMath::FRandRange(0.97f, 1.03f), 0.2f, 4.f);
	UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, FRotator::ZeroRotator, FinalVolume, FinalPitch, 0.f,
		Attenuation, Concurrency);
	PlayedNow.Add(FString::Printf(TEXT("%s(%.2f,%.2f)"), *Sound->GetName(), FinalVolume, FinalPitch));
}

void UToyFeedback::Haptic(EControllerHand Hand, float Delay, float Amplitude, float Attack, float Hold, float Decay) const
{
	if (Hand != EControllerHand::Left && Hand != EControllerHand::Right)
	{
		return;
	}
	if (AToyBoxPawn* Pawn = Cast<AToyBoxPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		FToyHapticLayer Layer;
		Layer.Delay = Delay;
		Layer.Amplitude = Amplitude;
		Layer.Attack = Attack;
		Layer.Hold = Hold;
		Layer.Decay = Decay;
		Pawn->AddHaptic(Hand, Layer);
	}
}

void UToyFeedback::Play(EToyFeedback Event, const FToyFeedbackParams& Params)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Params.Source && UsesCooldown(Event))
	{
		double& Last = LastPlayTime.FindOrAdd(TPair<const UObject*, uint8>(Params.Source, static_cast<uint8>(Event)), -10.0);
		if (Now - Last < SourceCooldown)
		{
			return;
		}
		Last = Now;
	}

	PlayedNow.Reset();
	const FVector& At = Params.Location;
	const EControllerHand Hand = Params.Hand;
	const EToyMaterial Material = Params.Material;
	const float Hard = Hardness(Material);
	// 大小：以 10cm 方块（1000 cm³）为准，体积每大 8 倍低 3 个半音；震动尾巴随体积变长
	const float Octaves = FMath::Log2(FMath::Max(Params.SizeCm3, 1.f) / 1000.f);
	const float SizePitch = FMath::Pow(2.f, FMath::Clamp(-Octaves, -8.f, 6.f) / 12.f);
	const float Tail = 1.f + 0.35f * FMath::Clamp(Octaves, 0.f, 4.f);

	switch (Event)
	{
	case EToyFeedback::HoverEnter:
		PlaySound(TEXT("Claw_HoverTick"), At, 0.25f, 1.f);
		Haptic(Hand, 0.f, 0.15f, 0.f, 0.010f, 0.f);
		break;
	case EToyFeedback::ServoTick:
		Haptic(Hand, 0.f, 0.12f, 0.f, 0.006f, 0.f);
		break;
	case EToyFeedback::FingerTouch:
		PlaySound(TapCue(Material), At, 0.45f * Hard, SizePitch);
		Haptic(Hand, 0.f, 0.2f + 0.3f * Hard, 0.f, 0.008f, HapticDecay(Material));
		break;
	case EToyFeedback::GrabSecured:
		PlaySound(TEXT("Claw_Lock"), At, 0.7f, 1.f);
		PlaySound(TapCue(Material), At, 0.8f * Hard, SizePitch * 0.85f);
		// 一下干脆的锁定 + 跟着材料和大小的余震
		Haptic(Hand, 0.f, 0.8f, 0.f, 0.012f, 0.02f);
		Haptic(Hand, 0.02f, 0.35f * Hard, 0.005f, 0.f, 0.06f * Tail);
		break;
	case EToyFeedback::GhostReady:
		PlaySound(TEXT("Part_Ready"), At, 0.35f, 1.f);
		Haptic(Hand, 0.f, 0.2f, 0.f, 0.02f, 0.04f);
		break;
	case EToyFeedback::ParkPulse:
		Haptic(Hand, 0.f, 0.18f + 0.25f * FMath::Clamp(Params.Speed, 0.f, 1.f), 0.f, 0.008f, 0.f);
		break;
	case EToyFeedback::Release:
	{
		const float Throw = FMath::Clamp(Params.Speed / 600.f, 0.f, 1.f);
		PlaySound(TEXT("Claw_Release"), At, 0.35f + 0.4f * Throw, 1.f + 0.1f * Throw);
		Haptic(Hand, 0.f, 0.2f + 0.5f * Throw, 0.005f, 0.02f, 0.08f);
		break;
	}
	case EToyFeedback::ReturnWhoosh:
		PlaySound(TEXT("Part_Whoosh"), At, FMath::Clamp(Params.Speed / 60.f, 0.15f, 0.8f), 0.9f + 0.2f * FMath::Clamp(Params.Speed / 60.f, 0.f, 1.f));
		break;
	case EToyFeedback::MagnetSnap:
		PlaySound(TEXT("Part_Seat"), At, 0.7f, SizePitch);
		PlaySound(TapCue(Material), At, 0.6f * Hard, SizePitch);
		// 双击：两下相隔 40ms
		Haptic(Hand, 0.f, 0.6f, 0.f, 0.010f, 0.015f);
		Haptic(Hand, 0.04f, 0.45f, 0.f, 0.010f, 0.015f);
		break;
	case EToyFeedback::Seat:
		PlaySound(TEXT("Part_Seat"), At, 0.45f, SizePitch);
		PlaySound(TapCue(Material), At, 0.35f * Hard, SizePitch);
		break;
	case EToyFeedback::UiHover:
		PlaySound(TEXT("UI_Hover"), At, 0.3f, 1.f);
		Haptic(Hand, 0.f, 0.12f, 0.f, 0.008f, 0.f);
		break;
	case EToyFeedback::UiPress:
		PlaySound(TEXT("UI_Press"), At, 0.7f, 1.f);
		Haptic(Hand, 0.f, 0.6f, 0.f, 0.012f, 0.02f);
		break;
	case EToyFeedback::UiRelease:
		PlaySound(TEXT("UI_Release"), At, 0.35f, 1.f);
		Haptic(Hand, 0.f, 0.25f, 0.f, 0.006f, 0.f);
		break;
	case EToyFeedback::MenuOpen:
		PlaySound(TEXT("UI_MenuOpen"), At, 0.5f, 1.f);
		break;
	case EToyFeedback::MenuClose:
		PlaySound(TEXT("UI_MenuClose"), At, 0.5f, 1.f);
		break;
	default:
		break;
	}

	if (CVarFeedbackLog.GetValueOnGameThread())
	{
		const TCHAR* HandName = Hand == EControllerHand::Left ? TEXT("左") : (Hand == EControllerHand::Right ? TEXT("右") : TEXT("-"));
		UE_LOG(LogTemp, Log, TEXT("ToyBox 反馈 %.3f: %s 手=%s 材料=%s 体积=%.0fcm³ 量=%.2f 来源=%s 声音=[%s]"), Now, EventName(Event), HandName,
			MaterialName(Material), Params.SizeCm3, Params.Speed, *GetNameSafe(Params.Source), *FString::Join(PlayedNow, TEXT(" ")));
	}
}

UAudioComponent* UToyFeedback::CreateServoLoop(USceneComponent* Parent)
{
	USoundBase* Sound = PickVariant(TEXT("Claw_ServoLoop"));
	if (!Sound || !Parent)
	{
		return nullptr;
	}
	EnsureSettings();
	UAudioComponent* Loop = NewObject<UAudioComponent>(Parent->GetOwner());
	Loop->SetSound(Sound);
	Loop->bAutoActivate = false;
	Loop->AttenuationSettings = Attenuation;
	Loop->SetupAttachment(Parent);
	Loop->RegisterComponent();
	return Loop;
}

void UToyFeedback::SetLoop(UAudioComponent* Loop, float Volume, float Pitch) const
{
	if (!Loop)
	{
		return;
	}
	const float Final = Volume * ToySettings::SoundGain();
	if (Final < 0.01f)
	{
		if (Loop->IsPlaying())
		{
			Loop->Stop();
		}
		return;
	}
	if (!Loop->IsPlaying())
	{
		Loop->Play();
	}
	Loop->SetVolumeMultiplier(Final);
	Loop->SetPitchMultiplier(Pitch);
}
