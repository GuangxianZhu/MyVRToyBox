#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyEtchContent.h"
#include "ToyEtchProcess.h"
#include "ToyEtcherRig.generated.h"

class AToyExplodedModel;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UToyShapeComponent;
class UWidgetComponent;
struct FToyEtchBrushes;

/** 放大反应腔里的一个粒子（网页版 parts[]）。坐标用网页版工艺腔的局部坐标：米，Y 朝上，晶圆表面 Y = 0.95，腔轴是 Y 轴 */
struct FToyEtchParticle
{
	/** 0 气体分子、1 电子、2 离子、3 自由基、4 产物 */
	uint8 Type = 0;
	FVector3f P = FVector3f::ZeroVector;
	FVector3f V = FVector3f::ZeroVector;
	float Age = 0.f;
	float Life = 8.f;
};

/** 剖面图上飞的离子（短线）和自由基（点），网页版 parts2d。坐标 nm：X 以栅极中心为 0，Y 从画面顶往下 */
struct FToyProfileDot
{
	bool bIon = false;
	float X = 0.f;
	float Y = 0.f;
	float VX = 0.f;
	float VY = 0.f;
	float Life = 1.f;
};

/**
 * 干法刻蚀机的"演示模式"：桌上摆出刻蚀机时由工作室生成，收起时一起删掉。照 ITBasics 干法刻蚀机网页版做了三级放大：
 *
 *   1. 桌上 60cm 的整机（AToyExplodedModel）：跑工艺时闸阀门落下、升降针顶起、真空机械手的手指托着晶圆从闸阀伸进伸出；
 *      起辉后腔里的紫色辉光透过腔壁看得见（X 光一样不被遮挡）。
 *   2. 左边放大的反应腔（真实尺寸的 60%，背面半圈内衬、介质窗、卡盘、晶圆）：网页版同一套粒子——
 *      气体分子从喷嘴进来，起辉后被电子打碎成离子和自由基，自由基吸附在晶圆上，离子被鞘层加速打下来生成 SiCl₄ 冒出，随气流被抽走。
 *   3. 再往上的剖面板：晶圆上一根 60nm 宽的栅极，刻蚀深度、侧壁钻蚀、栅氧、光刻胶都按 ToyEtchCore 实时画。
 *
 * 还有正前方的讲解板（第几步、现在在干什么、为什么 + 整个工艺的趋势曲线）和右边的结果板（5 项判定）。
 * 工艺本身是 ToyEtch::FProcess（15 步，和网页一样的时序），这里每帧按倍速推进它，再把状态画出来。按钮在触屏的刻蚀机页签里。
 *
 * 规矩和网页一样：跑工艺时不能拆开（爆炸），拆开的时候不能开始；放大腔和粒子只在装配状态下显示。
 */
UCLASS(NotPlaceable, Transient)
class VRTOYBOX_API AToyEtcherRig : public AActor
{
	GENERATED_BODY()

public:
	AToyEtcherRig();

	/** 工作室摆出刻蚀机后调用。Anchor = 模型支架的共同父节点（放大腔和面板挂在它上面，挪模型时一起走），PlayerEye = 玩家眼睛的大概位置（面板朝向它） */
	void Setup(AToyExplodedModel* InModel, USceneComponent* Anchor, const FVector& PlayerEye);

	/** 放大腔和面板转过来朝着玩家（挪过模型以后再调一次） */
	void AimAt(const FVector& PlayerEye);

	virtual void Tick(float DeltaSeconds) override;

	// ---------- 触屏按钮 ----------
	/** 开始 / 下一步 / 再跑一片。拆开着或者配方不对时不开始，讲解板上说原因 */
	void StartOrNext();
	void ToggleAuto();
	void TogglePause();
	void SetSpeedIndex(int32 Index);
	void ApplyPreset(int32 Index);
	/** 配方第 Field 个参数往上（Dir = 1）或往下（-1）调一格 */
	void StepField(int32 Field, int32 Dir);
	/** 回到待机（新的一片在晶圆盒里），上一片的结果留着 */
	void ResetProcess();
	void ToggleBoards();

	// ---------- 读状态（触屏和面板每帧读） ----------
	const ToyEtch::FProcess& GetProcess() const { return Process; }
	const FToyEtchContent& GetContent() const { return Content; }
	const TArray<FToyProfileDot>& GetProfileDots() const { return ProfileDots; }
	/** 平台小地图上晶圆点的位置（平台俯视坐标，米：X 向右，Y 朝晶圆盒那边） */
	FVector2D GetMapDot() const { return MapDot; }

	/** 一片正在跑（第 1 步开始到第 15 步做完）：配方锁住，不能拆开 */
	bool IsRunning() const { return Process.IsLocked(); }
	/** 桌上的模型完全装配着（拆开时不能开始工艺，也不显示放大腔） */
	bool IsAssembled() const;
	bool IsPaused() const { return bPaused; }
	bool IsAuto() const { return Process.bAuto; }
	int32 GetSpeedIndex() const { return SpeedIndex; }
	bool AreBoardsShown() const { return bBoardsShown; }
	/** 最后按过的配方参数（配方页底下显示它的提示） */
	int32 GetLastField() const { return LastField; }

	static constexpr int32 SpeedCount = 4;
	static float SpeedValue(int32 Index);

	// 讲解板
	FText ExplainTitle() const;
	FText ExplainNow() const;
	FText ExplainWhy() const;
	bool IsExplainError() const;

	// 触屏流程页
	FText StepName(int32 Index) const;
	/** 0 = 还没到，1 = 正在做，2 = 做完了 */
	int32 StepState(int32 Index) const;
	FText StartLabel() const;
	bool CanStart() const;
	FText StatusLine() const;
	FText TimeText() const;
	FText EndpointText() const;
	FText DepthText() const;

	// 触屏配方页
	FText FieldName(int32 Field) const;
	FText FieldValue(int32 Field) const;
	FText FieldTip() const;
	bool IsPresetSelected(int32 Index) const;
	FText PresetName(int32 Index) const;
	FText PredictText() const;

	// 结果
	bool HasResult() const { return Process.bHasResult; }
	FText VerdictText() const;
	bool IsVerdictOk() const;
	FText ResultLabel(int32 Index) const;
	FText ResultValue(int32 Index) const;
	bool IsResultOk(int32 Index) const;
	FText ResultHint(int32 Index) const;
	FText ResultStats() const;

	/** 讲解文字里 {名字} 的值：配方、摆阀开度、离子能量、过刻秒数、配方问题 */
	TMap<FString, FString> FillValues() const;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	/** 挂到桌上模型的 ModelRoot 上（模型坐标，厘米，跟着模型缩放）：辉光、真空机械手的手指 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> SmallRoot;

	/** 放大反应腔：原点在腔轴上、网页坐标 Y = 0.76 的高度，+X 背对玩家 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> MagRoot;

	/** 不受光照的粒子、辉光在自动曝光下太暗，乘这么多倍（和信息牌、触屏同一个原因） */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float EmissiveBoost = 10.f;

	/** 面板整体提亮倍数（和触屏一样） */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float BoardBrightness = 8.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BuildMaterials();
	void BuildBoards();
	void UpdateSmallModel();
	void UpdateMagnified(float Dt);
	void UpdateParticles(float Dt);
	void DrawParticles();
	void UpdateProfileDots(float Dt);
	void UpdateMapDot(float Dt);
	FToyEtchParticle* Spawn(uint8 Type, const FVector3f& P, const FVector3f& V);
	bool InWafer(float X, float Z) const;
	/** 网页坐标（米）→ 放大腔 MagRoot 的局部坐标（厘米） */
	FVector Mag(double X, double Y, double Z) const;

	UStaticMeshComponent* NewMesh(FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& SizeCm, const FVector& Location);
	UWidgetComponent* NewBoard(FName Name, const FVector2D& SizeCm, float PxPerCm);

	ToyEtch::FProcess Process;
	FToyEtchContent Content;

	TWeakObjectPtr<AToyExplodedModel> Model;
	bool bPaused = false;
	int32 SpeedIndex = 1;
	bool bBoardsShown = true;
	int32 LastField = INDEX_NONE;
	/** 上一次按开始时为什么没开始：配方问题（讲解板显示红字），或者模型拆开着 */
	ToyEtch::EProblem BadProblem = ToyEtch::EProblem::None;
	bool bNeedAssemble = false;
	bool bMagShown = true;

	TArray<FToyEtchParticle> Particles;
	float SpawnGas = 0.f;
	float Adsorbed = 0.f;
	TArray<FToyProfileDot> ProfileDots;
	FVector2D MapDot = FVector2D::ZeroVector;
	double LastWaferColorKey = -1.0;

	// ---------- 桌上小模型 ----------
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SmallGlow;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SmallGlowCore;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> SmallBlade;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> SmallBladeArm;

	// ---------- 放大反应腔 ----------
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagEsc;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagInsRing;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagEdgeRing;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagWafer;
	UPROPERTY()
	TArray<TObjectPtr<UToyShapeComponent>> MagPins;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagNozzle;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagBlade;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> MagBladeArm;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MagWindow;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MagBaffle;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MagLiner;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MagCoil;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MagGlow;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MagGlowCore;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> MagLight;
	/** 五种粒子各一个实例化网格：气体分子、电子、离子、自由基、产物 */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ParticleMeshes;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> Pedestal;
	UPROPERTY()
	TObjectPtr<UToyShapeComponent> PedestalBase;

	// ---------- 面板 ----------
	UPROPERTY()
	TObjectPtr<UWidgetComponent> InfoBoard;
	UPROPERTY()
	TObjectPtr<UWidgetComponent> ProfileBoard;
	UPROPERTY()
	TObjectPtr<UWidgetComponent> ResultBoard;
	UPROPERTY()
	TObjectPtr<UWidgetComponent> LegendBoard;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMaterials;

	TSharedPtr<FToyEtchBrushes> Brushes;
};
