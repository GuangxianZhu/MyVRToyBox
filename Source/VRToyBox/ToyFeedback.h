#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ToyFeedback.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;

/** 发声 / 震动用的材料大类（按零件 parts.json 的"材料"文字里第一个出现的关键词分，见 ClassifyMaterial）。 */
enum class EToyMaterial : uint8
{
	HeavyMetal,
	LightMetal,
	HardPlastic,
	Rubber,
	Brittle,
	/** 抓取器自己 */
	Claw
};

/** 所有声音 + 震动事件。每个事件在 UToyFeedback::Play 里配好用哪组声音、多响、手上怎么震。 */
enum class EToyFeedback : uint8
{
	/** 捏合点碰到能抓的东西 */
	HoverEnter,
	/** 扳机带着手指动，每合拢 / 张开约 0.08 一下（只震不响，声音是伺服电机循环） */
	ServoTick,
	/** 一根手指碰到零件表面（三根各一声，自然错开） */
	FingerTouch,
	/** 三根手指都到位：锁销"咔嗒" + 材料声 */
	GrabSecured,
	/** 拿着零件靠近原位、虚影出现 */
	GhostReady,
	/** 虚影出现后像倒车雷达一样的脉冲，越近越密（Speed = 0~1 的接近程度） */
	ParkPulse,
	/** 松手（Speed = 扔出速度 cm/s） */
	Release,
	/** 零件飞回原位（Speed = 离原位的距离 cm） */
	ReturnWhoosh,
	/** 离原位很近松手、吸回去落位（震松手的那只手） */
	MagnetSnap,
	/** 飞回原位后落位（手已经离远了，只响不震） */
	Seat,
	UiHover,
	UiPress,
	UiRelease,
	MenuOpen,
	MenuClose,
	Count
};

struct FToyFeedbackParams
{
	FVector Location = FVector::ZeroVector;
	/** 震哪只手；AnyHand = 不震。 */
	EControllerHand Hand = EControllerHand::AnyHand;
	EToyMaterial Material = EToyMaterial::HardPlastic;
	/** 包围盒体积（立方厘米）。越大音越低（8 倍体积 ≈ 低 3 个半音）、震动尾巴越长。 */
	float SizeCm3 = 1000.f;
	/** 事件自己的强度量，含义见 EToyFeedback。 */
	float Speed = 0.f;
	/** 同一个来源同一种事件 60ms 内只响一次。 */
	const UObject* Source = nullptr;
};

/**
 * 声音和震动的统一入口：Play(事件, 参数) 同一帧里放声音、给手加震动层（AToyBoxPawn 的震动混合器）。
 *
 * 声音是 Tools/audio/make_sounds.py 合成、导入到 /Game/Audio 的 wav，每种 3 个变体：
 * 轮着放（不和上一次一样）、音高 ±3%、音量 ±2 dB；3D 定位、离远变小；最多同时 12 个，超了停最早的。
 * 音量 / 震动强度乘设置页的 SOUND / HAPTICS 档位。控制台 toy.FeedbackLog 1 把每个事件打到日志。
 */
UCLASS()
class VRTOYBOX_API UToyFeedback : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UToyFeedback* Get(const UObject* WorldContext);

	void Play(EToyFeedback Event, const FToyFeedbackParams& Params);

	/** 伺服电机的循环声，挂在 Parent 上，默认不响；每帧用 SetLoop 调音量和音高（音量 0 时停掉）。 */
	UAudioComponent* CreateServoLoop(USceneComponent* Parent);
	void SetLoop(UAudioComponent* Loop, float Volume, float Pitch) const;

	static EToyMaterial ClassifyMaterial(const FString& Text);
	/** 抓得起来的东西的材料（IToyGrabbable::GetMaterialText）和包围盒体积。 */
	static EToyMaterial MaterialOf(const AActor* Actor);
	static float SizeOf(const AActor* Actor);
	static const TCHAR* MaterialName(EToyMaterial Material);

private:
	struct FCue
	{
		TArray<USoundBase*> Variants;
		int32 Last = INDEX_NONE;
	};

	void EnsureSettings();
	USoundBase* PickVariant(const FString& Cue);
	void PlaySound(const FString& Cue, const FVector& Location, float Volume, float Pitch);
	void Haptic(EControllerHand Hand, float Delay, float Amplitude, float Attack, float Hold, float Decay) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Loaded;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> Concurrency;

	TMap<FString, FCue> Cues;
	TMap<TPair<const UObject*, uint8>, double> LastPlayTime;
	/** 这一帧放过的声音名（日志用）。 */
	TArray<FString> PlayedNow;
};
