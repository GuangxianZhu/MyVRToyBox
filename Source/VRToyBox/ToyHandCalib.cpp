#include "ToyHandCalib.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	// 控制台变量直接绑在这几个 static float 上（FAutoConsoleVariableRef）：代码改值时直接写变量，
	// 不走 CVar 的 Set，避开"SetByCode 被 SetByConsole 拦下"的优先级问题
	float GPitch = ToyHandCalib::DefaultPitch;
	float GYaw = 0.f;
	float GRoll = 0.f;
	float GOffX = 0.f;
	float GOffY = 0.f;
	float GOffZ = 0.f;

	FAutoConsoleVariableRef CVarPitch(TEXT("toy.HandPitch"), GPitch, TEXT("VR 手模型绕握把 Y 轴（掌心法线）的旋转，度。默认 -64（UE VR 模板的值），合理范围 -55 ~ -85"));
	FAutoConsoleVariableRef CVarYaw(TEXT("toy.HandYaw"), GYaw, TEXT("VR 手模型绕握把 Z 轴的旋转，度（以右手为准，左手自动镜像）"));
	FAutoConsoleVariableRef CVarRoll(TEXT("toy.HandRoll"), GRoll, TEXT("VR 手模型绕握把 X 轴的旋转，度（以右手为准，左手自动镜像）"));
	FAutoConsoleVariableRef CVarOffX(TEXT("toy.HandOffsetX"), GOffX, TEXT("VR 手模型相对握把的平移 X，厘米"));
	FAutoConsoleVariableRef CVarOffY(TEXT("toy.HandOffsetY"), GOffY, TEXT("VR 手模型相对握把的平移 Y，厘米（以右手为准，左手自动镜像）"));
	FAutoConsoleVariableRef CVarOffZ(TEXT("toy.HandOffsetZ"), GOffZ, TEXT("VR 手模型相对握把的平移 Z，厘米"));

	FAutoConsoleCommand SaveCommand(TEXT("toy.HandCalibSave"),
		TEXT("把现在的 VR 手部校准存进 GameUserSettings.ini，并在日志里打一行 ToyBox HandCalib"),
		FConsoleCommandDelegate::CreateStatic(&ToyHandCalib::Save));

	const TCHAR* Section = TEXT("ToyBox.HandCalib");
}

ToyHandCalib::FParams ToyHandCalib::Get()
{
	FParams Params;
	Params.Pitch = GPitch;
	Params.Yaw = GYaw;
	Params.Roll = GRoll;
	Params.Offset = FVector(GOffX, GOffY, GOffZ);
	return Params;
}

void ToyHandCalib::Set(const FParams& Params)
{
	GPitch = Params.Pitch;
	GYaw = Params.Yaw;
	GRoll = Params.Roll;
	GOffX = static_cast<float>(Params.Offset.X);
	GOffY = static_cast<float>(Params.Offset.Y);
	GOffZ = static_cast<float>(Params.Offset.Z);
}

void ToyHandCalib::ResetDefaults()
{
	Set(FParams());
}

FTransform ToyHandCalib::HandTransform(EControllerHand Side)
{
	return HandTransform(Side, Get());
}

FTransform ToyHandCalib::HandTransform(EControllerHand Side, const FParams& Params)
{
	// 以右手为准；左手镜像：Pitch 不变，Yaw / Roll / OffsetY 取反（和 VDXR 的 flipHandedness 一样）
	const float M = Side == EControllerHand::Left ? -1.f : 1.f;
	const FQuat Rotation = FRotator(Params.Pitch, M * Params.Yaw, M * Params.Roll).Quaternion();
	// 绕掌心中心转：只有 Pitch 时支点在 Y 轴上，掌心中心不动
	const FVector Pivot(0.f, M * PalmY, 0.f);
	const FVector Offset(Params.Offset.X, M * Params.Offset.Y, Params.Offset.Z);
	return FTransform(Rotation, Pivot - Rotation.RotateVector(Pivot) + Offset);
}

void ToyHandCalib::Load()
{
	if (!GConfig)
	{
		return;
	}
	FParams Params = Get();
	float Value = 0.f;
	if (GConfig->GetFloat(Section, TEXT("Pitch"), Value, GGameUserSettingsIni)) Params.Pitch = Value;
	if (GConfig->GetFloat(Section, TEXT("Yaw"), Value, GGameUserSettingsIni)) Params.Yaw = Value;
	if (GConfig->GetFloat(Section, TEXT("Roll"), Value, GGameUserSettingsIni)) Params.Roll = Value;
	if (GConfig->GetFloat(Section, TEXT("OffsetX"), Value, GGameUserSettingsIni)) Params.Offset.X = Value;
	if (GConfig->GetFloat(Section, TEXT("OffsetY"), Value, GGameUserSettingsIni)) Params.Offset.Y = Value;
	if (GConfig->GetFloat(Section, TEXT("OffsetZ"), Value, GGameUserSettingsIni)) Params.Offset.Z = Value;
	Set(Params);
	UE_LOG(LogTemp, Log, TEXT("ToyBox HandCalib 读入: %s"), *Describe());
}

void ToyHandCalib::Save()
{
	if (GConfig)
	{
		GConfig->SetFloat(Section, TEXT("Pitch"), GPitch, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("Yaw"), GYaw, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("Roll"), GRoll, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("OffsetX"), GOffX, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("OffsetY"), GOffY, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("OffsetZ"), GOffZ, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	// 用户把这一行发回来，就写成代码默认值
	UE_LOG(LogTemp, Log, TEXT("ToyBox HandCalib: %s"), *Describe());
}

FString ToyHandCalib::Describe()
{
	return FString::Printf(TEXT("Pitch=%.1f Yaw=%.1f Roll=%.1f Off=(%.1f, %.1f, %.1f)"), GPitch, GYaw, GRoll, GOffX, GOffY, GOffZ);
}
