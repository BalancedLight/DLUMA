@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul && goto :toolchain_ready
if defined VS_INSTALL goto :use_vs_install
set "DLW_INV_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DLW_INV_VSWHERE%" goto :toolchain_missing
for /f "usebackq delims=" %%I in (`"%DLW_INV_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"

:use_vs_install
if not defined VS_INSTALL goto :toolchain_missing
if not exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto :toolchain_missing
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
where cl.exe >nul 2>nul || goto :toolchain_missing

:toolchain_ready
if not exist build\player-inventory-recorder-fake mkdir build\player-inventory-recorder-fake

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\player-inventory-recorder-fake/" ^
  /Fd"build\player-inventory-recorder-fake\player_inventory_recorder_fake_harness.pdb" ^
  tests\player_inventory_recorder_fake_harness.c ^
  src\dlw_player_inventory_recorder.c || exit /b 1

cl.exe /nologo /MT ^
  /Fe"build\player-inventory-recorder-fake\player_inventory_recorder_fake_harness.exe" ^
  build\player-inventory-recorder-fake\player_inventory_recorder_fake_harness.obj ^
  build\player-inventory-recorder-fake\dlw_player_inventory_recorder.obj ^
  kernel32.lib || exit /b 1

echo Built: build\player-inventory-recorder-fake\player_inventory_recorder_fake_harness.exe
exit /b 0

:toolchain_missing
echo ERROR: MSVC x64 toolchain not found. 1>&2
exit /b 1
