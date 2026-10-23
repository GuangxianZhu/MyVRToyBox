@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0.."

where git >nul 2>nul || (echo [更新] 没找到 git，跳过更新检查 & exit /b 0)
git rev-parse --is-inside-work-tree >nul 2>nul || (echo [更新] 这里不是 git 仓库，跳过 & exit /b 0)

echo [更新] 正在检查 main 分支有没有新提交...
git fetch origin main --quiet
if errorlevel 1 (
  echo [更新] 连不上 GitHub，先用本地版本继续
  exit /b 0
)

for /f %%i in ('git rev-parse HEAD') do set "LOCAL=%%i"
for /f %%i in ('git rev-parse origin/main') do set "REMOTE=%%i"
for /f %%i in ('git merge-base HEAD origin/main') do set "BASE=%%i"
for /f %%i in ('git branch --show-current') do set "BRANCH=%%i"

if "%LOCAL%"=="%REMOTE%" (
  echo [更新] 已经是最新的 main
  exit /b 0
)
if not "%BASE%"=="%LOCAL%" (
  echo [更新] 本地有 main 上没有的提交，不自动更新，免得冲突
  exit /b 0
)
if /i not "%BRANCH%"=="main" (
  echo [更新] main 有新提交，但你现在在分支 %BRANCH% 上，不自动切换
  exit /b 0
)
rem 本地有未提交的改动也照样拉：引擎总会往 DefaultEngine.ini 写 AndroidFileServer 段，以前因为它一直不更新。
rem git 自己会拦住会覆盖本地改动的拉取，所以这样是安全的
echo [更新] 发现新版本，正在拉取...
git pull --ff-only origin main
if errorlevel 1 (
  echo [更新] 拉取失败（可能是本地改动和新版本改了同一个文件），继续用本地版本。处理完改动后再运行 检查更新.bat
  exit /b 0
)
echo [更新] 已更新到最新 main
exit /b 0
