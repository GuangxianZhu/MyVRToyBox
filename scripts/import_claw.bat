@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem 三指抓取器（玩家手里的工具）：Tools\claw\build_claw_tool.py 生成 claw_tool.glb + rig.json → 导入成 UE 资产
rem 只有模型改了才需要跑；导入结果（Content\Models\ClawTool）会提交到仓库，平时不用管
rem 找得到 Blender 就先重新生成 glb，找不到就直接用仓库里的 Tools\claw\claw_tool.glb
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"
set "CLAW_DIR=%CD%\Tools\claw"

set "BLENDER="
if exist "%CLAW_DIR%\blender_path.txt" set /p BLENDER=<"%CLAW_DIR%\blender_path.txt"
if not defined BLENDER for /f "delims=" %%B in ('where blender 2^>nul') do if not defined BLENDER set "BLENDER=%%B"
if not defined BLENDER for /d %%D in ("%ProgramFiles%\Blender Foundation\Blender*") do if exist "%%D\blender.exe" set "BLENDER=%%D\blender.exe"
if defined BLENDER (
  echo [导入] 用 Blender 重新生成 claw_tool.glb ...
  "%BLENDER%" --background --factory-startup --python "%CLAW_DIR%\build_claw_tool.py" -- "%CLAW_DIR%"
) else (
  echo [导入] 没找到 Blender，直接用仓库里的 claw_tool.glb
)
if not exist "%CLAW_DIR%\claw_tool.glb" goto :fail
set "MODEL_GLB=%CLAW_DIR%\claw_tool.glb"
set "MODEL_NAME=ClawTool"

rem ---- UE 5.8 ----
if defined UE_ROOT goto :check_engine
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| find "InstalledDirectory"') do set "UE_ROOT=%%b"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
:check_engine
if not exist "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (
  echo [导入] 找不到 UE 5.8：%UE_ROOT%
  goto :fail
)

echo [导入] 编译 C++ 模块 ...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" VRToyBoxEditor Win64 Development -Project="%PROJECT%" -WaitMutex
if errorlevel 1 goto :fail

echo [导入] 导入到 UE ...
rem -script= 遇到带空格的路径（Unreal Projects）会被拆成两段，所以只传一行不含空格的 Python，脚本路径走环境变量
set "MOTOR_IMPORT_SCRIPT=%CD%\Tools\import_model.py"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['MOTOR_IMPORT_SCRIPT']) -unattended -nosplash -nullrhi
if not exist "%CD%\Content\Models\ClawTool\parts.json" goto :fail
rem 手指关节表：导入会清掉整个 ClawTool 目录，所以导入完再拷过去
copy /y "%CLAW_DIR%\rig.json" "%CD%\Content\Models\ClawTool\rig.json" >nul
echo [导入] 完成。Content\Models\ClawTool 里是导入的资产，rig.json 是手指关节表
pause
exit /b 0

:fail
echo [导入] 失败，看上面的输出
pause
exit /b 1
