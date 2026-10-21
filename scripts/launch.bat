@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem 用法：launch.bat editor  打开编辑器（默认）
rem       launch.bat vr      不开编辑器，直接以游戏模式进头显
set "MODE=%~1"
if "%MODE%"=="" set "MODE=editor"
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"

call "%~dp0update.bat"

rem 找 UE 5.8：优先环境变量 UE_ROOT，其次 Epic 启动器写的注册表，最后默认安装路径
if defined UE_ROOT goto :check_engine
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| find "InstalledDirectory"') do set "UE_ROOT=%%b"
if defined UE_ROOT goto :check_engine
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

:check_engine
set "EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%EDITOR%" (
  echo [启动] 找不到 UE 5.8：%EDITOR%
  echo [启动] 装在别处的话，先设置环境变量 UE_ROOT 指向引擎根目录，例如 D:\Epic\UE_5.8
  pause
  exit /b 1
)

echo [启动] 编译 C++ 模块，第一次会比较久...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" VRToyBoxEditor Win64 Development -Project="%PROJECT%" -WaitMutex
if errorlevel 1 (
  echo [启动] 编译失败。最常见的原因：没装 Visual Studio 2022 的「使用 C++ 的游戏开发」工作负载
  pause
  exit /b 1
)

if /i "%MODE%"=="vr" (
  echo [启动] 以 VR 游戏模式启动，请先打开 SteamVR 或 Meta Quest Link
  start "" "%EDITOR%" "%PROJECT%" -game -vr
) else (
  echo [启动] 打开编辑器。点工具栏 Play 旁边的下拉菜单选 VR Preview 就能进头显
  start "" "%EDITOR%" "%PROJECT%"
)
exit /b 0
