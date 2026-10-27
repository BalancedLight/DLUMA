@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
set "OUT_DIR=%~1"
if "%OUT_DIR%"=="" set "OUT_DIR=build\managed-bootstrap-harness"
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat >nul || exit /b 1
)
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
cl.exe /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE ^
  tests\managed_bootstrap_harness.cpp ^
  /Fo:"%OUT_DIR%\managed_bootstrap_harness.obj" ^
  /Fe:"%OUT_DIR%\managed_bootstrap_harness.exe"
if errorlevel 1 exit /b 1
exit /b 0
