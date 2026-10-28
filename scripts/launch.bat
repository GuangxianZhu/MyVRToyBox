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

rem 装了 ArchViz 样板间（只在本机，不进仓库）并生成过 L_StudioArchViz 就进样板间；想回原来的方块房间，先 set TOY_CLASSIC=1
set "MAP="
if not "%TOY_CLASSIC%"=="1" if exist "%CD%\Content\ArchViz\Maps\L_StudioArchViz.umap" set "MAP=/Game/ArchViz/Maps/L_StudioArchViz"
if defined MAP (
  echo [启动] 场景：ArchViz 样板间 %MAP%
) else (
  echo [启动] 场景：默认工作室 L_Studio
)

if /i "%MODE%"=="vr" (
  echo [启动] 以 VR 游戏模式启动，请先打开 SteamVR 或 Meta Quest Link
  start "" "%EDITOR%" "%PROJECT%" %MAP% -game -vr
) else (
  echo [启动] 打开编辑器。点工具栏 Play 旁边的下拉菜单选 VR Preview 就能进头显
  start "" "%EDITOR%" "%PROJECT%" %MAP%
)
exit /b 0
