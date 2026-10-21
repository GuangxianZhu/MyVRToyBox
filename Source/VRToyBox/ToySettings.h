#pragma once

#include "CoreMinimal.h"

/**
 * 玩家设置（主菜单的 SETTINGS 页改它）：转身方式、移动速度、传送开关、捏合小球、抓取范围、渲染分辨率。
 *
 * 每个选项都是"几档里选一档"，菜单按钮每按一次换到下一档。Pawn 和手每帧读 Get()，所以改了马上生效；
 * 渲染分辨率是控制台变量 r.ScreenPercentage，改的时候由 Apply() 设一次。
 * 存在 GameUserSettings.ini 的 [ToyBox.Settings]（和手部校准同一个文件），开局 Load() 读回。
 */
namespace ToySettings
{
	enum class ETurn : uint8 { Snap45, Snap30, Smooth, Count };

	struct FSettings
	{
		ETurn Turn = ETurn::Snap45;
		/** 0 = 慢，1 = 正常，2 = 快。 */
		int32 MoveSpeed = 1;
		bool bTeleport = true;
		bool bPinchBall = true;
		/** 0 = 小，1 = 中（原来的 2cm），2 = 大。 */
		int32 GrabSize = 1;
		/** 0 = 80%，1 = 100%，2 = 120%。 */
		int32 Resolution = 1;
	};

	const FSettings& Get();
	void Set(const FSettings& Settings);

	/** 换到下一档，设完自动 Apply + Save。 */
	void CycleTurn();
	void CycleMoveSpeed();
	void ToggleTeleport();
	void TogglePinchBall();
	void CycleGrabSize();
	void CycleResolution();

	/** 换算成具体数值。 */
	float SnapTurnDegrees();
	float MoveSpeedCm();
	float GrabRadiusCm();
	float ScreenPercentage();

	/** 菜单按钮第二行的文字（英文：TextRender 自带字体没有中文字形）。 */
	FString TurnLabel();
	FString MoveSpeedLabel();
	FString TeleportLabel();
	FString PinchBallLabel();
	FString GrabSizeLabel();
	FString ResolutionLabel();

	/** 把要设控制台变量的选项（渲染分辨率）设下去。 */
	void Apply();
	void Load();
	void Save();
	FString Describe();
}
