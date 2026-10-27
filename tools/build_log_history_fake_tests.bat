@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\log-history-fake mkdir build\log-history-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\log-history-fake/" ^
  /Fd"build\log-history-fake\log_history_fake_harness.pdb" ^
  /Fe"build\log-history-fake\log_history_fake_harness.exe" ^
  tests\log_history_fake_harness.c ^
  src\dlw_log_history_core.c src\dlw_logs_menu_core.c || exit /b 1

build\log-history-fake\log_history_fake_harness.exe || exit /b 1
echo Built and passed: build\log-history-fake\log_history_fake_harness.exe
exit /b 0
