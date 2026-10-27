@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul && goto :toolchain_ready
if defined VS_INSTALL goto :use_vs_install
set "DLW_PLAYERDI_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DLW_PLAYERDI_VSWHERE%" goto :toolchain_missing
for /f "usebackq delims=" %%I in (`"%DLW_PLAYERDI_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"

:use_vs_install
if not defined VS_INSTALL goto :toolchain_missing
if not exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto :toolchain_missing
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
where cl.exe >nul 2>nul || goto :toolchain_missing

:toolchain_ready
if not exist build\playerdi-teardown-fake mkdir build\playerdi-teardown-fake

ml64.exe /nologo /c ^
  /Fo"build\playerdi-teardown-fake\playerdi_teardown_abi_masm.obj" ^
  tests\playerdi_teardown_abi_masm.asm || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\playerdi-teardown-fake/" ^
  /Fd"build\playerdi-teardown-fake\playerdi_teardown_fake_harness.pdb" ^
  tests\playerdi_teardown_fake_harness.c ^
  src\dlw_playerdi_teardown.c || exit /b 1

cl.exe /nologo /MT ^
  /Fe"build\playerdi-teardown-fake\playerdi_teardown_fake_harness.exe" ^
  build\playerdi-teardown-fake\playerdi_teardown_fake_harness.obj ^
  build\playerdi-teardown-fake\dlw_playerdi_teardown.obj ^
  build\playerdi-teardown-fake\playerdi_teardown_abi_masm.obj ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\playerdi-teardown-fake\playerdi_teardown_fake_harness.exe
exit /b 0

:toolchain_missing
echo ERROR: an initialized MSVC x64 toolchain was not found.
exit /b 1
