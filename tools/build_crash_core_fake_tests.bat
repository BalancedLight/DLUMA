@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul && goto :toolchain_ready
if defined VS_INSTALL goto :use_vs_install
set "DLW_CRASH_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DLW_CRASH_VSWHERE%" goto :toolchain_missing
for /f "usebackq delims=" %%I in (`"%DLW_CRASH_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"

:use_vs_install
if not defined VS_INSTALL goto :toolchain_missing
if not exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto :toolchain_missing
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
where cl.exe >nul 2>nul || goto :toolchain_missing

:toolchain_ready

if not exist "build\crash-core-fake" mkdir "build\crash-core-fake"
cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\crash-core-fake\\" ^
  /Fd"build\crash-core-fake\crash_core_fake_harness.pdb" ^
  /Fe"build\crash-core-fake\crash_core_fake_harness.exe" ^
  tests\crash_core_fake_harness.c || exit /b 1

"build\crash-core-fake\crash_core_fake_harness.exe" || exit /b 1
echo Built: build\crash-core-fake\crash_core_fake_harness.exe

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"build\crash-core-fake\crash_worker_stack_fake_harness.obj" ^
  /Fd"build\crash-core-fake\crash_worker_stack_fake_harness.pdb" ^
  /Fe"build\crash-core-fake\crash_worker_stack_fake_harness.exe" ^
  tests\crash_worker_stack_fake_harness.c || exit /b 1

"build\crash-core-fake\crash_worker_stack_fake_harness.exe" || exit /b 1
echo Built: build\crash-core-fake\crash_worker_stack_fake_harness.exe
exit /b 0

:toolchain_missing
echo ERROR: an initialized MSVC x64 toolchain was not found.
exit /b 1
