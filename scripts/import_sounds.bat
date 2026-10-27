@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem 音效：Tools\audio\make_sounds.py 合成 wav（Tools\audio\out）→ 导入成 UE 资产（Content\Audio）
rem 只有改了音效才需要跑；导入结果会提交到仓库，平时不用管
rem 找得到 Python（带 numpy）就先重新合成，找不到就直接用仓库里的 wav
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"

where python >nul 2>nul && (
  echo [音效] 重新合成 wav ...
  python "%CD%\Tools\audio\make_sounds.py"
)
if not exist "%CD%\Tools\audio\out\UI_Press_1.wav" goto :fail

rem ---- UE 5.8 ----
if defined UE_ROOT goto :check_engine
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| find "InstalledDirectory"') do set "UE_ROOT=%%b"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
:check_engine
if not exist "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (
  echo [音效] 找不到 UE 5.8：%UE_ROOT%
  goto :fail
)

echo [音效] 编译 C++ 模块 ...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" VRToyBoxEditor Win64 Development -Project="%PROJECT%" -WaitMutex
if errorlevel 1 goto :fail

echo [音效] 导入到 UE ...
rem -script= 遇到带空格的路径（Unreal Projects）会被拆成两段，所以只传一行不含空格的 Python，脚本路径走环境变量
set "SOUND_IMPORT_SCRIPT=%CD%\Tools\audio\import_sounds.py"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['SOUND_IMPORT_SCRIPT']) -unattended -nosplash -nullrhi
if not exist "%CD%\Content\Audio\UI_Press_1.uasset" goto :fail
echo [音效] 完成。Content\Audio 里是导入的音效
pause
exit /b 0

:fail
echo [音效] 失败，看上面的输出
pause
exit /b 1
