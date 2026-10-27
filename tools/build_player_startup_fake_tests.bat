@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\player-startup-fake mkdir build\player-startup-fake

cl.exe /nologo /TC /MT /O2 /W4 /WX ^
  /Fo"build\player-startup-fake/" ^
  /Fd"build\player-startup-fake\player_startup_fake_harness.pdb" ^
  /Fe"build\player-startup-fake\player_startup_fake_harness.exe" ^
  tests\player_startup_fake_harness.c ^
  src\dlw_player_startup_core.c ^
  /link /stack:8388608 kernel32.lib || exit /b 1

echo Built: build\player-startup-fake\player_startup_fake_harness.exe
exit /b 0
