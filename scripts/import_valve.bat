@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem 气动蝶阀模型：three.js 写的爆炸图（codex_localAI\work\valve）导出 valve.glb → 导入成 UE 资产 + 爆炸图清单
rem 只有模型改了才需要跑；导入结果（Content\Models\Valve）会提交到仓库，平时不用管
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"

rem ---- 先检查 VRToyBox 自己 main 上有没有更新 ----
call "%~dp0update.bat"

rem ---- 阀门工程：环境变量 VALVE_DIR，否则用这台电脑上的默认位置（它不在 git 里，换电脑要先拷过去） ----
if not defined VALVE_DIR set "VALVE_DIR=C:\work\codex_localAI\work\valve"
if not exist "%VALVE_DIR%\src\10-parts.js" (
  echo [导入] 找不到阀门工程：%VALVE_DIR%
  echo [导入] 设置环境变量 VALVE_DIR 指向 work\valve 目录（里面有 src\ 和 build.mjs，上一级有 node_modules）
  goto :fail
)
where node >nul 2>nul
if errorlevel 1 (
  echo [导入] 找不到 Node.js，装一个再来
  goto :fail
)

set "VALVE_GLB=%VALVE_DIR%\.build\valve.glb"
echo [导入] 用无头 Chrome 跑阀门页面，导出 valve.glb ...
node "%CD%\Tools\export_valve_glb.mjs" "%VALVE_DIR%" "%VALVE_GLB%"
if not exist "%VALVE_GLB%" goto :fail
set "MODEL_GLB=%VALVE_GLB%"
set "MODEL_NAME=Valve"

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
if not exist "%CD%\Content\Models\Valve\parts.json" goto :fail
rem 虚影 / 高亮用的半透明材质，已经有了就跳过
set "MOTOR_IMPORT_SCRIPT=%CD%\Tools\create_toy_materials.py"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['MOTOR_IMPORT_SCRIPT']) -unattended -nosplash -nullrhi
echo [导入] 完成。Content\Models\Valve 里是导入的资产，parts.json 是爆炸图清单
pause
exit /b 0

:fail
echo [导入] 失败，看上面的输出
pause
exit /b 1
