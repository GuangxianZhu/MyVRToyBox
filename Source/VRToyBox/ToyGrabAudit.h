#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyStudio.h"
#include "ToyGrabAudit.generated.h"

class AToyBoxPawn;
class AToyExplodedModel;
class AToyPartUnit;

/**
 * 调试命令 toy.GrabAudit [Motor|Valve|Drone|Lathe|Multimeter|Server|Robot|Engine|All] [quit]：桌面模式下自动把每个抓取单元抓一遍并截图。
 *
 * 每个模型：摆出来 → 等着色器编译完 → 爆炸 → 对每个能抓的单元：
 *   右手从镜头方向伸过去，捏合点放在零件上离镜头最近的表面点（和桌面模式鼠标点中零件一样），扣扳机 → 走正常的捏取流程；
 *   抓住后手往上提一点离开模型，镜头从手的右前方稍低处往回看，给指尖、掌心和零件一个特写，截图到 Saved/GrabAudit/<模型>/<序号>_<单元>.png；
 *   松手，等零件飞回去，镜头回原位，下一个。
 * 同时记 summary.csv：抓法、有没有抓到、手掌 / 捏合点离零件真实表面多远（抓空气的零件这个数会很大）。
 * 跑的时候 Pawn 的桌面输入停掉，跑完恢复；带 quit 参数时跑完退出游戏（命令行批量跑用）。
 */
UCLASS(NotPlaceable, Transient)
class VRTOYBOX_API AToyGrabAudit : public AActor
{
	GENERATED_BODY()

public:
	AToyGrabAudit();

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
		WaitModel,
		WaitExplode,
		Approach,
		Close,
		Present,
		Shoot,
		Release,
		Finish
	};

	void Goto(EStep Next, float Delay = 0.f);
	void BeginUnit();
	void PresentHeld();
	void NextModel();
	void Finish();
	void WriteSummary();
	void AimOverview();
	float SurfaceGap(const FVector& From) const;

	TArray<EToyModelKind> Kinds;
	int32 KindIndex = -1;
	bool bQuitWhenDone = false;

	TWeakObjectPtr<AToyBoxPawn> Pawn;
	TWeakObjectPtr<AToyStudio> Studio;
	TWeakObjectPtr<AToyExplodedModel> Model;
	TArray<TWeakObjectPtr<AToyPartUnit>> Queue;
	int32 UnitIndex = -1;

	EStep Step = EStep::FindPlayer;
	float Wait = 0.f;
	float Elapsed = 0.f;

	/** 镜头原来的相对变换（跑完恢复）和每个模型对准后的相对变换（每次特写完回到这里再伸手）。 */
	FTransform CameraOriginal;
	FTransform CameraHome;

	/** 每个模型先拍一张装配、一张爆炸的全景（00_assembled.png / 00_exploded.png），再开始逐个抓。 */
	bool bShotAssembled = false;
	bool bShotExploded = false;

	FString OutDir;
	TArray<FString> Rows;
	FString CurrentRow;
	int32 Grabbed = 0;
	int32 Missed = 0;
};
