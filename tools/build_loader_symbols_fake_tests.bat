@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul && goto :toolchain_ready
if defined VS_INSTALL goto :use_vs_install
set "DLW_SYMBOLS_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DLW_SYMBOLS_VSWHERE%" goto :toolchain_missing
for /f "usebackq delims=" %%I in (`"%DLW_SYMBOLS_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"

:use_vs_install
if not defined VS_INSTALL goto :toolchain_missing
if not exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto :toolchain_missing
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
where cl.exe >nul 2>nul || goto :toolchain_missing

:toolchain_ready
if not exist "build\loader-symbols-fake" mkdir "build\loader-symbols-fake"
cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\loader-symbols-fake\\" ^
  /Fd"build\loader-symbols-fake\loader_symbols_fake_harness.pdb" ^
  /Fe"build\loader-symbols-fake\loader_symbols_fake_harness.exe" ^
  tests\loader_symbols_fake_harness.c ^
  src\dlw_loader_symbols.c ^
  /link kernel32.lib user32.lib || exit /b 1

"build\loader-symbols-fake\loader_symbols_fake_harness.exe" || exit /b 1
echo Built: build\loader-symbols-fake\loader_symbols_fake_harness.exe
exit /b 0

:toolchain_missing
echo ERROR: an initialized MSVC x64 toolchain was not found.
exit /b 1
