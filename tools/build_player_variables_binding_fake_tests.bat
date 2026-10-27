@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\player-variables-binding-fake (
  mkdir build\player-variables-binding-fake || exit /b 1
)

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fe"build\player-variables-binding-fake\player_variables_binding_write_fake_harness.exe" ^
  /Fo"build\player-variables-binding-fake\player_variables_binding_write_fake_harness.obj" ^
  tests\player_variables_binding_write_fake_harness.c /link kernel32.lib || exit /b 1

build\player-variables-binding-fake\player_variables_binding_write_fake_harness.exe || exit /b 1
exit /b 0
