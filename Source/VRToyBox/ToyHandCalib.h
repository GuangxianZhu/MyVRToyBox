#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * VR 里手模型相对控制器握把姿态（LeftGrip / RightGrip）的校准。
 *
 * 为什么要有：手模型按"手指沿握把姿态 +X"摆（桌面模式和 toy.HandPreview 也靠这个约定），但 OpenXR 的 grip 姿态
 * 换到 UE 后，+X 是握拳时四指围成的"管子"轴（沿着握把），不是手指指的方向。所以 VR 里手要绕掌心法线（grip Y）
 * 往下转约 64°（UE 5.7/5.8 VR 模板对同一只 MannyXR 手用的就是这个角度）。
 *
 * 校准只加在手 Actor 挂到控制器上的相对变换上（AToyBoxPawn::ApplyMode / TickVR），HandMesh 的摆法不动，
 * 所以桌面模式完全不受影响。参数以右手为准，左手镜像：Pitch 不变，Yaw / Roll / OffsetY 取反。
 *
 * 调参：控制台 toy.HandPitch / HandYaw / HandRoll / HandOffsetX / HandOffsetY / HandOffsetZ（度、厘米，grip 系），
 * 或者 VR 里长按左摇杆进校准模式。toy.HandCalibSave 存到 GameUserSettings.ini 的 [ToyBox.HandCalib]，开局读回。
 */
namespace ToyHandCalib
{
	/** 掌心中心在握把姿态里的 Y 偏移（厘米，右手 +，左手 -）。手模型和校准的旋转支点共用。 */
	constexpr float PalmY = 3.f;

	constexpr float DefaultPitch = -64.f;

	struct FParams
	{
		float Pitch = DefaultPitch;
		float Yaw = 0.f;
		float Roll = 0.f;
		FVector Offset = FVector::ZeroVector;

		bool operator==(const FParams& Other) const
		{
			return Pitch == Other.Pitch && Yaw == Other.Yaw && Roll == Other.Roll && Offset == Other.Offset;
		}
	};

	/** 现在的参数（控制台变量绑在同一组 static float 上）。 */
	FParams Get();
	void Set(const FParams& Params);
	void ResetDefaults();

	/** 手 Actor 相对握把姿态的变换：绕掌心中心转，再平移。左手镜像。 */
	FTransform HandTransform(EControllerHand Side);
	FTransform HandTransform(EControllerHand Side, const FParams& Params);

	/** 读 / 存 GameUserSettings.ini 的 [ToyBox.HandCalib]；Save 时打一行 "ToyBox HandCalib: ..."。 */
	void Load();
	void Save();
	FString Describe();
}
