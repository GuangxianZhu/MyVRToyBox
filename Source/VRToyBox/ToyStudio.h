#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.generated.h"

class AToyCube;
class AToyMenuPanel;
class UChildActorComponent;
class AToyExplodedModel;

/** 桌上能摆出来的爆炸图模型。一次只摆一个，换模型时先收起原来的，所以不会互相重叠。 */
enum class EToyModelKind : uint8
{
	Motor,
	Valve,
	Drone,
	Lathe,
	Multimeter,
	Server,
	Orb,
	Engine,
	DexHand,
	Etcher
};

/**
 * 6m x 6m 的小工作室：地板、四面墙、天花板、灯，以及一张桌子。
 * Actor 原点 = 玩家站立的位置（地面高度），玩家面朝 +X，桌子就在正前方。
 *
 * 房间、桌子和触屏（ChildActorComponent）都在构造函数里建，所以把它拖进任何关卡，编辑器视口里立刻能看到；
 * 会动的东西（方块、爆炸图模型、手）还是运行时生成。默认关卡 Content/Maps/L_Studio 里就只放了一个它。
 */
UCLASS()
class VRTOYBOX_API AToyStudio : public AActor
{
	GENERATED_BODY()

public:
	AToyStudio();

	/** 在桌上 SpawnPoint 附近掉下一个随机颜色的方块。数量到上限时返回 false。 */
	bool SpawnCube();
	void ClearCubes();
	int32 GetCubeCount() const;

	/** 触屏按钮的入口。 */
	void HandleScreenAction(EToyScreenAction Action);

	/** 摆出 / 收起某个模型。桌上已经有别的模型时先收起它。资产还没导入时返回 false。 */
	bool ToggleModel(EToyModelKind Kind);

	/** 桌上的模型在爆炸 / 装配之间切换；桌上没有模型时先摆出电机。 */
	void ToggleModelExploded();

	/** 手里拿着的零件 / 整机全部松开，飞回原位。 */
	void ResetModel();

	/** 把桌上的模型整体挪一下（厘米，工作室坐标：-X 拉近、+Y 往右），限制在桌面范围里。 */
	void MoveModel(const FVector& DeltaCm);

	/** 模型离站立点多远（厘米），状态栏显示。 */
	float GetModelDistanceCm() const;
	bool IsModelShown(EToyModelKind Kind) const;
	bool IsModelExploded() const;

	/** 屏幕底部那行状态文字。 */
	FString GetStatusText() const;

	static FString KindName(EToyModelKind Kind);

	bool HasModel() const { return Model != nullptr; }

	/** 上次想摆出来但资产还没导入的模型（触屏顶栏提示要跑哪个导入脚本），摆成功一个就清掉。 */
	TOptional<EToyModelKind> GetMissingKind() const { return MissingKind; }

	/** 桌上现在摆着的模型，没有就是空（调试命令 toy.GrabAudit 用）。 */
	AToyExplodedModel* GetModel() const { return Model; }

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	int32 MaxCubes = 20;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> SpawnPoint;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ScreenMount;

	/** 触屏：作为子 Actor 在构造时就建出来（编辑器视口里看得见），BeginPlay 时接上按钮回调。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UChildActorComponent> ScreenChild;

	/** 所有模型支架的共同父节点，触屏的挪动按钮挪它。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ModelAnchor;

	/** 模型的默认位置（工作室坐标，厘米）：离站立点 70cm，偏左 10cm，桌面高度。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FVector ModelHome = FVector(70.f, -10.f, 75.f);

	/** 挪动按钮每按一次走多远（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MoveStepCm = 10.f;

	/** 电机放在这里：桌面中间，电机轴沿桌子长边，爆炸时零件往左右两边飞。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> MotorMount;

	/** 球阀放在同一个位置（一次只摆一个模型），转 90°：管道 / 执行器缸体长轴沿桌子长边，爆炸时端盖、活塞往左右两边飞。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ValveMount;

	/** 零件清单，相对 Content 目录。由 Tools/import_model.py 生成。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString MotorManifest = TEXT("Models/Motor/parts.json");

	/** 无人机也放在同一个位置，转 90° 让机头（相机）朝着玩家。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> DroneMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString ValveManifest = TEXT("Models/Valve/parts.json");

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString DroneManifest = TEXT("Models/Drone/parts.json");

	/** 迷你车床也放在同一个位置，转 90°：床身沿桌子长边，正面（电机、手轮把手）朝着玩家，主轴箱在右手边。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> LatheMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString LatheManifest = TEXT("Models/Lathe/parts.json");

	/** 万用表也放在同一个位置，面朝上平放，转 90°：屏幕那头朝远处、表笔插孔朝着玩家，红表笔在右手边。
	 *  展示尺寸 40cm（写在它的 parts.json 里，不用 ModelLengthCm）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> MultimeterMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString MultimeterManifest = TEXT("Models/Multimeter/parts.json");

	/** AI 服务器：Yaw 180，前面板（风扇）朝左手边、后面（电源、网卡）朝右手边，侧面朝着玩家。
	 *  拆开后 GPU 托盘往左上抽出、主板托盘往右后抽出（避开右手边的触屏）。展示尺寸 55cm（写在它的 parts.json 里）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> ServerMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString ServerManifest = TEXT("Models/AIServer/parts.json");

	/** 球形漂浮机器人 ORB-1：Yaw 90，主眼（模型 +Y）朝着玩家，涡轮发电机（模型 +X）在右手边、电子舱在左手边。
	 *  模型里自带停机坪，机器人悬在坪上方约 15cm。展示尺寸 50cm（含停机坪的总高）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> OrbMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString OrbManifest = TEXT("Models/OrbBot/parts.json");

	/** 直列四缸发动机：Yaw 90，进气侧（模型 +Y）朝着玩家，正时端（模型 -X）在左手边、飞轮在右手边。
	 *  展示尺寸 60cm（装配后最长的是高度）。往远处挪 6cm：拆开后油底壳抽屉往玩家这边拉出约 35cm，正好停在桌子前沿。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> EngineMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString EngineManifest = TEXT("Models/Engine/parts.json");

	/** 五指灵巧手（右手）：法兰朝下立在桌上，Yaw 90 让手心（模型 +Y）朝着玩家，拇指在玩家右手边。
	 *  展示尺寸 50cm（装配后最长的是高度，实物约 21cm）。拆开后手指往上、电机往玩家这边弹，背盖、腕部、法兰往后退。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> DexHandMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString DexHandManifest = TEXT("Models/DexHand/parts.json");

	/** 干法刻蚀机（ICP 工艺腔 PM1）：Yaw 90 让闸阀（模型 +Y）朝着玩家，气体盒、分子泵在玩家右手边。
	 *  展示尺寸 60cm（装配后最长的是高度，实物约 1.7m）。拆开后源组件退到后面排一排，腔里的件摆到左边，晶圆飘到闸阀上方。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> EtcherMount;

	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString EtcherManifest = TEXT("Models/Etcher/parts.json");

	/** 模型装配状态下最长边的长度（厘米）。模型清单里写了 length_cm 的以清单为准（万用表 40、AI 服务器 55、发动机 60、灵巧手 50、刻蚀机 60）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ModelLengthCm = 50.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AToyTouchScreen> Screen;

	/** 主菜单面板：开局生成并打开，触屏的 MENU 再叫出来。 */
	UPROPERTY(Transient)
	TObjectPtr<AToyMenuPanel> Menu;

	/** 桌上现在摆着的模型（没有就是空）和它是哪一种。 */
	UPROPERTY(Transient)
	TObjectPtr<AToyExplodedModel> Model;

	EToyModelKind ModelKind = EToyModelKind::Motor;

	TArray<TWeakObjectPtr<AToyCube>> Cubes;

	TOptional<EToyModelKind> MissingKind;
};
