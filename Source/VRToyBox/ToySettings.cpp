#include "ToySettings.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	ToySettings::FSettings GSettings;

	const TCHAR* SettingsSection = TEXT("ToyBox.Settings");

	constexpr float MoveSpeeds[3] = { 100.f, 150.f, 220.f };
	constexpr float GrabRadii[3] = { 1.5f, 2.f, 3.f };
	constexpr float Percentages[3] = { 80.f, 100.f, 120.f };

	void Changed()
	{
		ToySettings::Apply();
		ToySettings::Save();
	}

	FAutoConsoleCommand DescribeCommand(TEXT("toy.Settings"), TEXT("在日志里打一行现在的玩家设置"),
		FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogTemp, Log, TEXT("ToyBox Settings: %s"), *ToySettings::Describe()); }));
}

const ToySettings::FSettings& ToySettings::Get()
{
	return GSettings;
}

void ToySettings::Set(const FSettings& Settings)
{
	GSettings = Settings;
	GSettings.MoveSpeed = FMath::Clamp(GSettings.MoveSpeed, 0, 2);
	GSettings.GrabSize = FMath::Clamp(GSettings.GrabSize, 0, 2);
	GSettings.Resolution = FMath::Clamp(GSettings.Resolution, 0, 2);
	if (GSettings.Turn >= ETurn::Count)
	{
		GSettings.Turn = ETurn::Snap45;
	}
}

void ToySettings::CycleTurn()
{
	GSettings.Turn = static_cast<ETurn>((static_cast<uint8>(GSettings.Turn) + 1) % static_cast<uint8>(ETurn::Count));
	Changed();
}

void ToySettings::CycleMoveSpeed() { GSettings.MoveSpeed = (GSettings.MoveSpeed + 1) % 3; Changed(); }
void ToySettings::ToggleTeleport() { GSettings.bTeleport = !GSettings.bTeleport; Changed(); }
void ToySettings::TogglePinchBall() { GSettings.bPinchBall = !GSettings.bPinchBall; Changed(); }
void ToySettings::CycleGrabSize() { GSettings.GrabSize = (GSettings.GrabSize + 1) % 3; Changed(); }
void ToySettings::CycleResolution() { GSettings.Resolution = (GSettings.Resolution + 1) % 3; Changed(); }

float ToySettings::SnapTurnDegrees()
{
	return GSettings.Turn == ETurn::Snap30 ? 30.f : 45.f;
}

float ToySettings::MoveSpeedCm() { return MoveSpeeds[GSettings.MoveSpeed]; }
float ToySettings::GrabRadiusCm() { return GrabRadii[GSettings.GrabSize]; }
float ToySettings::ScreenPercentage() { return Percentages[GSettings.Resolution]; }

FString ToySettings::TurnLabel()
{
	switch (GSettings.Turn)
	{
	case ETurn::Snap30: return TEXT("SNAP 30");
	case ETurn::Smooth: return TEXT("SMOOTH");
	default:            return TEXT("SNAP 45");
	}
}

FString ToySettings::MoveSpeedLabel()
{
	static const TCHAR* Names[3] = { TEXT("SLOW"), TEXT("NORMAL"), TEXT("FAST") };
	return Names[GSettings.MoveSpeed];
}

FString ToySettings::TeleportLabel() { return GSettings.bTeleport ? TEXT("ON") : TEXT("OFF"); }
FString ToySettings::PinchBallLabel() { return GSettings.bPinchBall ? TEXT("SHOW") : TEXT("HIDE"); }

FString ToySettings::GrabSizeLabel()
{
	static const TCHAR* Names[3] = { TEXT("SMALL"), TEXT("MEDIUM"), TEXT("LARGE") };
	return Names[GSettings.GrabSize];
}

FString ToySettings::ResolutionLabel()
{
	return FString::Printf(TEXT("%.0f%%"), ScreenPercentage());
}

void ToySettings::Apply()
{
	// r.ScreenPercentage 在 VR 里同样管两只眼的渲染分辨率；SetByCode 低于 ini 和控制台，所以 ini 里别写死它
	if (IConsoleVariable* Percentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		Percentage->Set(ScreenPercentage(), ECVF_SetByCode);
	}
}

void ToySettings::Load()
{
	if (GConfig)
	{
		FSettings Settings = GSettings;
		int32 Value = 0;
		bool bValue = false;
		if (GConfig->GetInt(SettingsSection, TEXT("Turn"), Value, GGameUserSettingsIni)) Settings.Turn = static_cast<ETurn>(FMath::Clamp(Value, 0, 2));
		if (GConfig->GetInt(SettingsSection, TEXT("MoveSpeed"), Value, GGameUserSettingsIni)) Settings.MoveSpeed = Value;
		if (GConfig->GetBool(SettingsSection, TEXT("Teleport"), bValue, GGameUserSettingsIni)) Settings.bTeleport = bValue;
		if (GConfig->GetBool(SettingsSection, TEXT("PinchBall"), bValue, GGameUserSettingsIni)) Settings.bPinchBall = bValue;
		if (GConfig->GetInt(SettingsSection, TEXT("GrabSize"), Value, GGameUserSettingsIni)) Settings.GrabSize = Value;
		if (GConfig->GetInt(SettingsSection, TEXT("Resolution"), Value, GGameUserSettingsIni)) Settings.Resolution = Value;
		Set(Settings);
	}
	Apply();
	UE_LOG(LogTemp, Log, TEXT("ToyBox Settings 读入: %s"), *Describe());
}

void ToySettings::Save()
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetInt(SettingsSection, TEXT("Turn"), static_cast<int32>(GSettings.Turn), GGameUserSettingsIni);
	GConfig->SetInt(SettingsSection, TEXT("MoveSpeed"), GSettings.MoveSpeed, GGameUserSettingsIni);
	GConfig->SetBool(SettingsSection, TEXT("Teleport"), GSettings.bTeleport, GGameUserSettingsIni);
	GConfig->SetBool(SettingsSection, TEXT("PinchBall"), GSettings.bPinchBall, GGameUserSettingsIni);
	GConfig->SetInt(SettingsSection, TEXT("GrabSize"), GSettings.GrabSize, GGameUserSettingsIni);
	GConfig->SetInt(SettingsSection, TEXT("Resolution"), GSettings.Resolution, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogTemp, Log, TEXT("ToyBox Settings: %s"), *Describe());
}

FString ToySettings::Describe()
{
	return FString::Printf(TEXT("Turn=%s Move=%s(%.0f) Teleport=%s PinchBall=%s Grab=%s(%.1fcm) Res=%s"),
		*TurnLabel(), *MoveSpeedLabel(), MoveSpeedCm(), *TeleportLabel(), *PinchBallLabel(), *GrabSizeLabel(), GrabRadiusCm(),
		*ResolutionLabel());
}
