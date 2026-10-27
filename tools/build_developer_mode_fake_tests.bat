@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\developer-mode-fake mkdir build\developer-mode-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\developer-mode-fake/" ^
  /Fd"build\developer-mode-fake\developer_mode_core_harness.pdb" ^
  /Fe"build\developer-mode-fake\developer_mode_core_harness.exe" ^
  tests\developer_mode_core_harness.c src\dlw_developer_mode_core.c || exit /b 1

echo Built: build\developer-mode-fake\developer_mode_core_harness.exe
exit /b 0
