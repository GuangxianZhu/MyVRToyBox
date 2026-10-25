#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyStudio.h"
#include "ToyDemoRecorder.generated.h"

class AToyBoxPawn;
class AToyExplodedModel;
class AToyPartUnit;

/**
 * 调试命令 toy.Demo [Motor|Valve|Drone|Lathe|Multimeter|Server|Robot|Engine|Hand|Etcher ...] [quit]：录 README 用的演示短片（逐帧截图，之后用 ffmpeg 拼成 GIF）。
 *
 * 每个模型：摆出来 → 等着色器编译完 → 开始逐帧截图到 Saved/Demo/<模型>/f_0000.png …：
 *   装配停一会 → 爆炸 → 右手从镜头这边伸过去捏起一个零件、往上提，信息牌浮出来 → 松手，零件飞回 → 装配回去。
 *   镜头全程绕模型慢慢转一点。
 * 要拿到匀速的帧，命令行带 -benchmark -fps=15（固定步长：每帧都是 1/15 秒游戏时间，截图慢也不掉帧）。
 */
UCLASS(NotPlaceable, Transient)
class VRTOYBOX_API AToyDemoRecorder : public AActor
{
	GENERATED_BODY()

public:
	AToyDemoRecorder();

	void Start(const TArray<EToyModelKind>& InKinds, bool bInQuitWhenDone);

	virtual void Tick(float DeltaSeconds) override;

	static bool IsRunning(const UWorld* World);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStep : uint8
	{
		FindPlayer,
		ShowModel,
		WaitShaders,
		Measure,
		Record,
		Finish
	};

	void NextModel();
	void Finish();
	void PickFeatured();
	void AimCamera(float Time);
	FBox UnitBounds() const;
	/** 录制时间线：按 Time（秒）摆好这一帧的模型、手和镜头，返回 false = 这个模型录完了。 */
	bool ApplyTimeline(float Time);

	TArray<EToyModelKind> Kinds;
	int32 KindIndex = -1;
	bool bQuitWhenDone = false;

	TWeakObjectPtr<AToyBoxPawn> Pawn;
	TWeakObjectPtr<AToyStudio> Studio;
	TWeakObjectPtr<AToyExplodedModel> Model;
	TWeakObjectPtr<AToyPartUnit> Featured;

	EStep Step = EStep::FindPlayer;
	float Wait = 0.f;
	float Elapsed = 0.f;
	float RecordTime = 0.f;
	int32 Frame = 0;
	FString OutDir;

	/** 时间线的关键时刻（秒），按模型的爆炸时长算。 */
	float TExplode = 0.f;
	float TReach = 0.f;
	float TLift = 0.f;
	float TRelease = 0.f;
	float TAssemble = 0.f;
	float TEnd = 0.f;

	/** 被抓零件上离镜头最近的表面点和从哪个方向伸手过去（爆炸完那一刻量）。 */
	FVector GrabPoint = FVector::ZeroVector;
	FVector ReachFrom = FVector::ZeroVector;
	FRotator HandFacing = FRotator::ZeroRotator;
	FVector HeldHandStart = FVector::ZeroVector;
	bool bGrabMeasured = false;
	bool bReleased = false;
	bool bAssembling = false;

	FTransform CameraOriginal;

	/** 录之前先爆炸一次再装回去，量出装配 / 爆炸两种状态的包围盒，镜头按爆炸进度在两种取景之间拉远拉近。 */
	FBox AssembledBounds;
	FBox ExplodedBounds;
	int32 MeasurePhase = 0;
};
