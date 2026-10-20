@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem 三相电机模型：ITBasics 的 Blender 脚本生成 motor.glb → 导入成 UE 资产 + 爆炸图清单
rem 只有模型改了才需要跑；导入结果（Content\Models\Motor）会提交到仓库，平时不用管
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"

rem ---- ITBasics：环境变量 ITBASICS_DIR，否则用同级目录 ..\ITBasics，没有就 clone 一份 ----
if not defined ITBASICS_DIR set "ITBASICS_DIR=%CD%\..\ITBasics"
if not exist "%ITBASICS_DIR%\三相电机爆炸图\build_motor.py" (
  echo [导入] 没找到 ITBasics，克隆到 %ITBASICS_DIR%
  git clone --depth 1 https://github.com/GuangxianZhu/ITBasics "%ITBASICS_DIR%"
  if errorlevel 1 goto :fail
) else (
  git -C "%ITBASICS_DIR%" pull --ff-only origin main
)
set "MOTOR_DIR=%ITBASICS_DIR%\三相电机爆炸图"

rem ---- Blender：blender_path.txt → PATH → Program Files ----
set "BLENDER="
if exist "%MOTOR_DIR%\blender_path.txt" set /p BLENDER=<"%MOTOR_DIR%\blender_path.txt"
if not defined BLENDER for /f "delims=" %%B in ('where blender 2^>nul') do if not defined BLENDER set "BLENDER=%%B"
if not defined BLENDER for /d %%D in ("%ProgramFiles%\Blender Foundation\Blender*") do if exist "%%D\blender.exe" set "BLENDER=%%D\blender.exe"
if not defined BLENDER (
  echo [导入] 找不到 Blender。把 blender.exe 的完整路径写进 %MOTOR_DIR%\blender_path.txt
  goto :fail
)

set "MOTOR_OUT=%MOTOR_DIR%\output"
echo [导入] 用 Blender 生成 motor.glb ...
"%BLENDER%" --background --factory-startup --python "%MOTOR_DIR%\build_motor.py" -- "%MOTOR_OUT%"
if not exist "%MOTOR_OUT%\motor.glb" goto :fail
set "MODEL_GLB=%MOTOR_OUT%\motor.glb"
set "MODEL_NAME=Motor"

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
if not exist "%CD%\Content\Models\Motor\parts.json" goto :fail
rem 虚影 / 高亮用的半透明材质，已经有了就跳过
set "MOTOR_IMPORT_SCRIPT=%CD%\Tools\create_toy_materials.py"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['MOTOR_IMPORT_SCRIPT']) -unattended -nosplash -nullrhi
echo [导入] 完成。Content\Models\Motor 里是导入的资产，parts.json 是爆炸图清单
pause
exit /b 0

:fail
echo [导入] 失败，看上面的输出
pause
exit /b 1
