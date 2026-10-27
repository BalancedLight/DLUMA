@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLW_STEAM_FAKE_DIR=build\steam-presence-fake"
if not exist "%DLW_STEAM_FAKE_DIR%" mkdir "%DLW_STEAM_FAKE_DIR%"

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_STEAM_FAKE_DIR%\steam_presence_fake_harness.obj" ^
  /Fd"%DLW_STEAM_FAKE_DIR%\steam_presence_fake_harness.pdb" ^
  tests\steam_presence_fake_harness.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_STEAM_FAKE_DIR%\dlw_presence_core.obj" ^
  /Fd"%DLW_STEAM_FAKE_DIR%\dlw_presence_core.pdb" ^
  src\dlw_presence_core.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_STEAM_FAKE_DIR%\dlw_steam_presence_core.obj" ^
  /Fd"%DLW_STEAM_FAKE_DIR%\dlw_steam_presence_core.pdb" ^
  src\dlw_steam_presence_core.c || exit /b 1
link.exe /nologo ^
  /OUT:"%DLW_STEAM_FAKE_DIR%\steam_presence_fake_harness.exe" ^
  "%DLW_STEAM_FAKE_DIR%\steam_presence_fake_harness.obj" ^
  "%DLW_STEAM_FAKE_DIR%\dlw_presence_core.obj" ^
  "%DLW_STEAM_FAKE_DIR%\dlw_steam_presence_core.obj" || exit /b 1

echo Built: %DLW_STEAM_FAKE_DIR%\steam_presence_fake_harness.exe
exit /b 0
