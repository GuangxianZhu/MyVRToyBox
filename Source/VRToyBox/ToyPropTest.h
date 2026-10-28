#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyPropTest.generated.h"

class AToyBoxPawn;
class AToyProp;
class AToyStudio;

/**
 * 调试命令 toy.PropTest [quit]：桌面模式下把样板间的摆件（AToyProp）全部自检一遍，截图到 Saved/ArchVizCheck/。
 *
 *   1. 开局 2 秒：每件离开局位置多远（睡着的摆件应该一动不动）；再把它们全部叫醒等 2 秒，看有没有自己跳 / 滑 / 掉下去（碰撞互相顶着会弹开）
 *   2. 每一件：镜头对准，右手捏合点放到它离镜头最近的表面上 → 高亮截图 → 扣扳机 → 抓到没有 → 手往上提 20cm（摆件跟没跟上）→ 截图
 *      → 松手，等 2 秒：落回原来的桌面 / 架子上（没穿下去）
 *   3. 全部从 1 米高丢到站立点身后的空地上：落地、没穿过地板
 *   4. 触屏「摆件复位」：全部回到原位
 *   5. 原位看 3 秒，记平均帧时间
 * 结果写日志（ToyBox: PropTest ...）和 Saved/ArchVizCheck/props_summary.csv；带 quit 跑完退出。
 */
UCLASS(NotPlaceable, Transient)
class VRTOYBOX_API AToyPropTest : public AActor
{
	GENERATED_BODY()

public:
	AToyPropTest();

	void Start(bool bInQuitWhenDone);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 排一步：等 Delay 秒再执行。 */
	void Then(float Delay, TFunction<void()> Step);
	void Plan();
	void PlanProp(int32 Index);
	void Shot(const FString& Name);
	void AimAt(const FVector& Target, float Distance, float Height);
	FVector Center(const AToyProp* Prop) const;
	/** 摆件正下方第一个面（不算摆件）的高度 */
	float SupportZ(const AToyProp* Prop) const;
	void Fail(const FString& What);
	void Finish();

	struct FTimedStep
	{
		float Delay;
		TFunction<void()> Run;
	};
	TArray<FTimedStep> Steps;
	int32 StepIndex = 0;
	float Elapsed = 0.f;

	TWeakObjectPtr<AToyBoxPawn> Pawn;
	TWeakObjectPtr<AToyStudio> Studio;
	TArray<TWeakObjectPtr<AToyProp>> Props;
	TArray<FTransform> Homes;
	/** 开局时摆件下面那个台面的高度（SupportZ） */
	TArray<float> HomeBottoms;
	TArray<FString> Rows;
	FTransform CameraOriginal;
	FString OutDir;
	int32 Failures = 0;
	bool bQuitWhenDone = false;
	bool bStarted = false;

	/** 帧时间统计 */
	bool bMeasuring = false;
	double FrameTimeSum = 0.0;
	int32 FrameCount = 0;
};
