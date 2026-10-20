#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyTouchScreen.h"
#include "ToyStudio.generated.h"

class AToyCube;
class AToyExplodedModel;

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

	/** 在桌上显示 / 收起三相电机模型。资产还没导入时返回 false。 */
	bool ToggleMotor();
	void ToggleMotorExploded();

	/** 手里拿着的零件 / 整机全部松开，飞回原位。 */
	void ResetMotor();
	bool IsMotorShown() const;
	bool IsMotorExploded() const;

	/** 屏幕底部那行状态文字。 */
	FString GetStatusText() const;

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

	/** 电机零件清单，相对 Content 目录。由 Tools/import_motor.py 生成。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	FString MotorManifest = TEXT("Models/Motor/parts.json");

	/** 电机装配状态下最长边的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "ToyBox")
	float MotorLengthCm = 50.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AToyTouchScreen> Screen;

	UPROPERTY(Transient)
	TObjectPtr<AToyExplodedModel> Motor;

	TArray<TWeakObjectPtr<AToyCube>> Cubes;
	bool bMotorMissing = false;
};
