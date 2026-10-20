#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.generated.h"

class AToyCube;
class AToyExplodedModel;

/** 桌上能摆出来的爆炸图模型。一次只摆一个，换模型时先收起原来的，所以不会互相重叠。 */
enum class EToyModelKind : uint8
{
	Motor,
	Valve,
	Drone
};

/**
 * 6m x 6m 的小工作室：地板、四面墙、天花板、灯，以及一张桌子。
 * Actor 原点 = 玩家站立的位置（地面高度），玩家面朝 +X，桌子就在正前方。
 *
 * 房间和桌子在构造函数里建，所以把它拖进任何关卡，编辑器里立刻能看到；
 * 触屏在 BeginPlay 时生成并放到 ScreenMount 上。
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
	bool IsModelShown(EToyModelKind Kind) const;
	bool IsModelExploded() const;

	/** 屏幕底部那行状态文字。 */
	FString GetStatusText() const;

	static FString KindName(EToyModelKind Kind);

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

	/** 模型装配状态下最长边的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float ModelLengthCm = 50.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AToyTouchScreen> Screen;

	/** 桌上现在摆着的模型（没有就是空）和它是哪一种。 */
	UPROPERTY(Transient)
	TObjectPtr<AToyExplodedModel> Model;

	EToyModelKind ModelKind = EToyModelKind::Motor;

	TArray<TWeakObjectPtr<AToyCube>> Cubes;

	/** 上次想摆出来但资产没导入的模型名，状态栏提示用。 */
	FString MissingModel;
};
