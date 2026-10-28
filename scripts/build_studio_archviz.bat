@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem ArchViz 样板间工作室：把 Epic 的 ArchViz Interior 示例（本机 Content\ArchViz，不进仓库）改成 L_StudioArchViz，
rem 餐桌换成我们的桌子和触屏，再用 CPU Lightmass 烘焙光照（GPU Lightmass 会崩）。烘焙要几十分钟
rem 生成之后 一键启动.bat 会自动进这个样板间；想回原来的方块房间，先 set TOY_CLASSIC=1
cd /d "%~dp0.."
set "PROJECT=%CD%\VRToyBox.uproject"

if not exist "%CD%\Content\ArchViz\Maps\ArchVis_Lightmap.umap" (
  echo [样板间] 没找到 Content\ArchViz\Maps\ArchVis_Lightmap.umap
  echo [样板间] 先从 Fab 把 ArchViz Interior 示例加到一个工程，再把它的 Content 整个拷成这里的 Content\ArchViz
  goto :fail
)

call "%~dp0update.bat"

rem ---- UE 5.8 ----
if defined UE_ROOT goto :check_engine
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul ^| find "InstalledDirectory"') do set "UE_ROOT=%%b"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
:check_engine
if not exist "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (
  echo [样板间] 找不到 UE 5.8：%UE_ROOT%
  goto :fail
)

echo [样板间] 编译 C++ 模块 ...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" VRToyBoxEditor Win64 Development -Project="%PROJECT%" -WaitMutex
if errorlevel 1 goto :fail

echo [样板间] 生成 L_StudioArchViz 并烘焙光照（CPU Lightmass Production，要几十分钟）...
rem -script= 遇到带空格的路径（Unreal Projects）会被拆成两段，所以只传一行不含空格的 Python，脚本路径走环境变量
set "ARCHVIZ_SCRIPT=%CD%\Tools\archviz\build_studio_archviz.py"
set "ARCHVIZ_STEP=build"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['ARCHVIZ_SCRIPT']) -unattended -nosplash -nullrhi
if errorlevel 1 goto :fail
if not exist "%CD%\Content\ArchViz\Maps\L_StudioArchViz.umap" goto :fail
rem 烘焙要渲染（反射捕获、光照贴图编码），不能 -nullrhi
set "ARCHVIZ_STEP=bake"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['ARCHVIZ_SCRIPT']) -unattended -nosplash -AllowCommandletRendering
if errorlevel 1 goto :fail
if not exist "%CD%\Content\ArchViz\Maps\L_StudioArchViz_BuiltData.uasset" goto :fail
echo [样板间] 完成。双击 一键启动.bat 或 一键启动_VR直玩.bat 就进样板间工作室
pause
exit /b 0

:fail
echo [样板间] 失败，看上面的输出
pause
exit /b 1
