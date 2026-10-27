@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\player-vitals-fake mkdir build\player-vitals-fake || exit /b 1
cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fe"build\player-vitals-fake\player_vitals_service_fake_harness.exe" ^
  /Fo"build\player-vitals-fake\player_vitals_service_fake_harness.obj" ^
  tests\player_vitals_service_fake_harness.c /link kernel32.lib || exit /b 1
build\player-vitals-fake\player_vitals_service_fake_harness.exe || exit /b 1
exit /b 0
