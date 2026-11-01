@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\config-legacy mkdir build\config-legacy || exit /b 1
cl /nologo /TC /MT /O2 /W4 /WX /Fobuild\config-legacy\test.obj /Febuild\config-legacy\test.exe tests\config_legacy_default_harness.c || exit /b 1
build\config-legacy\test.exe || exit /b 1
