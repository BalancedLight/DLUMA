@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\quest-hud-service-fake mkdir build\quest-hud-service-fake
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fe"build\quest-hud-service-fake\quest_hud_service_fake_harness.exe" ^
  /Fo"build\quest-hud-service-fake\quest_hud_service_fake_harness.obj" ^
  tests\quest_hud_service_fake_harness.c || exit /b 1
build\quest-hud-service-fake\quest_hud_service_fake_harness.exe || exit /b 1
