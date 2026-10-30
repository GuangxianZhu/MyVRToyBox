@echo off
chcp 65001 >nul
setlocal EnableExtensions
rem ArchViz 样板间工作室：把 Epic 的 ArchViz Interior 示例（本机 Content\ArchViz，不进仓库）改成 L_StudioArchViz，
rem 餐桌换成我们的桌子和触屏，再用 CPU Lightmass 烘焙光照（GPU Lightmass 会崩）。烘焙要几十分钟
rem 生成之后 一键启动.bat 会自动进这个样板间；想回原来的方块房间，先 set TOY_CLASSIC=1
rem 装了 Home Interior（Content\Home_Interior）的话，它接在样板间前门外（子关卡 L_StudioArchViz_Home），穿过门厅走过去就到，两边一起烘焙
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
rem 上次生成的 Home 子关卡、开了门洞的房子网格先删文件：编辑器里删它们有时会被拒绝（"An asset already exists"）。
rem L_HomeStudio 是以前"穿门换关卡"那一版生成的，现在不用了
del /q "%CD%\Content\Home_Interior\Maps\L_StudioArchViz_Home.umap" "%CD%\Content\Home_Interior\Maps\L_StudioArchViz_Home_BuiltData.uasset" 2>nul
del /q "%CD%\Content\Home_Interior\Meshes\SM_Scene_ToyDoor.uasset" 2>nul
del /q "%CD%\Content\ArchViz\BuildingMeshes\SM_Wall_Front_ToyDoor.uasset" 2>nul
del /q "%CD%\Content\Home_Interior\Maps\L_HomeStudio.umap" "%CD%\Content\Home_Interior\Maps\L_HomeStudio_BuiltData.uasset" 2>nul
rem 门厅的地板、墙、天花板网格是每次重新生成的
if exist "%CD%\Content\ArchViz\ToyHall" rd /s /q "%CD%\Content\ArchViz\ToyHall"
rem -script= 遇到带空格的路径（Unreal Projects）会被拆成两段，所以只传一行不含空格的 Python，脚本路径走环境变量
set "ARCHVIZ_SCRIPT=%CD%\Tools\archviz\build_studio_archviz.py"
set "ARCHVIZ_STEP=build"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['ARCHVIZ_SCRIPT']) -unattended -nosplash -nullrhi
if errorlevel 1 goto :fail
if not exist "%CD%\Content\ArchViz\Maps\L_StudioArchViz.umap" goto :fail

rem Fab 上的 Home Interior（拷在 Content\Home_Interior）：接到样板间前门外
if not exist "%CD%\Content\Home_Interior\Maps\Home_Interior.umap" (
  echo [样板间] 没有 Content\Home_Interior，前门关着，不接别的房子
  goto :bake
)
echo [样板间] 把 Home Interior 接到前门外（子关卡 L_StudioArchViz_Home + 门厅）...
set "ARCHVIZ_STEP=home"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['ARCHVIZ_SCRIPT']) -unattended -nosplash -nullrhi
if errorlevel 1 goto :fail
if not exist "%CD%\Content\Home_Interior\Maps\L_StudioArchViz_Home.umap" goto :fail

:bake
rem 默认 CPU Lightmass（约 18 分钟）。set ARCHVIZ_BAKER=gpu 试 GPU Lightmass（实验性，光照贴图约 1 分钟）：UE 5.8 在这个工程里
rem 体积光照贴图一直是 0 个 brick（试过前向 / 延迟渲染、开关 Nanite），摆件、小人偶、手会全黑；脚本发现 0 个 brick 就判失败、不存盘，接着用 CPU 烘
if /i not "%ARCHVIZ_BAKER%"=="gpu" goto :bake_cpu
echo [样板间] GPU Lightmass 烘焙（实验性，失败会自动改用 CPU）...
set "ARCHVIZ_STEP=gpubake"
del /q "%CD%\Saved\ArchVizGpuBake.txt" 2>nul
rem 普通编辑器里跑（GPU Lightmass 要出帧，命令行模式不行）；-ExecutePythonScript 跑完脚本就会退出编辑器，所以用 -ExecCmds 调 py（路径不能带空格，拷到 TEMP）
> "%TEMP%\archviz_gpubake.py" echo import os, runpy; runpy.run_path(os.environ['ARCHVIZ_SCRIPT'])
rem 光追只在这次启动里开（-ini 覆盖，DefaultEngine.ini 不动），要 SM6；编辑器在后台时不降帧；-unattended 让"升级光照贴图 UV"之类的对话框自动选默认（否）
start "" /wait "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%" -dx12 -sm6 -nohmd -nosplash -unattended "-ini:Engine:[/Script/Engine.RendererSettings]:r.RayTracing=True,[/Script/Engine.RendererSettings]:r.SkinCache.CompileShaders=True" "-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False" "-ExecCmds=py %TEMP%\archviz_gpubake.py"
if exist "%CD%\Saved\ArchVizGpuBake.txt" goto :done
echo [样板间] GPU 烘焙没成功，改用 CPU Lightmass

:bake_cpu
rem 烘焙要渲染（反射捕获、光照贴图编码），不能 -nullrhi
set "ARCHVIZ_STEP=bake"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['ARCHVIZ_SCRIPT']) -unattended -nosplash -AllowCommandletRendering
if errorlevel 1 goto :fail
if not exist "%CD%\Content\ArchViz\Maps\L_StudioArchViz_BuiltData.uasset" goto :fail

:done
echo [样板间] 完成。双击 一键启动.bat 或 一键启动_VR直玩.bat 就进样板间工作室
pause
exit /b 0

:fail
echo [样板间] 失败，看上面的输出
pause
exit /b 1
