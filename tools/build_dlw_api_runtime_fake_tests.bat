@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

set "DLW_ROOT=%~dp0.."
for %%I in ("%DLW_ROOT%") do set "DLW_ROOT=%%~fI"

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1

set "DLW_OUT=%DLW_ROOT%\build\dlw-api-runtime-fake"

if not exist "%DLW_OUT%" mkdir "%DLW_OUT%"
if errorlevel 1 exit /b 1

cl.exe /nologo /TC /std:c17 /W4 /WX /O2 /MT ^
  /I"%DLW_ROOT%\src" ^
  /I"%DLW_ROOT%\sdk\c" ^
  "%DLW_ROOT%\tests\dlw_api_runtime_fake_harness.c" ^
  "%DLW_ROOT%\src\core\dlw_mod_owner.c" ^
  "%DLW_ROOT%\src\core\dlw_capability_registry.c" ^
  "%DLW_ROOT%\src\core\dlw_api_runtime.c" ^
  /Fo"%DLW_OUT%\\" ^
  /Fe"%DLW_OUT%\dlw_api_runtime_fake_harness.exe"
if errorlevel 1 exit /b 1

"%DLW_OUT%\dlw_api_runtime_fake_harness.exe"
exit /b %ERRORLEVEL%
