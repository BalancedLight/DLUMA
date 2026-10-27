@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\chrome-log-fake mkdir build\chrome-log-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\chrome-log-fake/" ^
  /Fd"build\chrome-log-fake\chrome_log_core_fake_harness.pdb" ^
  /Fe"build\chrome-log-fake\chrome_log_core_fake_harness.exe" ^
  tests\chrome_log_core_fake_harness.c src\dlw_chrome_log_core.c || exit /b 1

build\chrome-log-fake\chrome_log_core_fake_harness.exe || exit /b 1
echo Built and passed: build\chrome-log-fake\chrome_log_core_fake_harness.exe
exit /b 0
