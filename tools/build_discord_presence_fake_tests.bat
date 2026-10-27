@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLW_DISCORD_FAKE_DIR=build\discord-presence-fake"
if not exist "%DLW_DISCORD_FAKE_DIR%" mkdir "%DLW_DISCORD_FAKE_DIR%"

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_DISCORD_FAKE_DIR%\discord_presence_fake_harness.obj" ^
  /Fd"%DLW_DISCORD_FAKE_DIR%\discord_presence_fake_harness.pdb" ^
  tests\discord_presence_fake_harness.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_DISCORD_FAKE_DIR%\dlw_discord_presence_core.obj" ^
  /Fd"%DLW_DISCORD_FAKE_DIR%\dlw_discord_presence_core.pdb" ^
  src\dlw_discord_presence_core.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_DISCORD_FAKE_DIR%\dlw_presence_core.obj" ^
  /Fd"%DLW_DISCORD_FAKE_DIR%\dlw_presence_core.pdb" ^
  src\dlw_presence_core.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"%DLW_DISCORD_FAKE_DIR%\dlw_editor_presence_core.obj" ^
  /Fd"%DLW_DISCORD_FAKE_DIR%\dlw_editor_presence_core.pdb" ^
  src\dlw_editor_presence_core.c || exit /b 1
link.exe /nologo ^
  /OUT:"%DLW_DISCORD_FAKE_DIR%\discord_presence_fake_harness.exe" ^
  "%DLW_DISCORD_FAKE_DIR%\discord_presence_fake_harness.obj" ^
  "%DLW_DISCORD_FAKE_DIR%\dlw_discord_presence_core.obj" ^
  "%DLW_DISCORD_FAKE_DIR%\dlw_presence_core.obj" ^
  "%DLW_DISCORD_FAKE_DIR%\dlw_editor_presence_core.obj" || exit /b 1

echo Built: %DLW_DISCORD_FAKE_DIR%\discord_presence_fake_harness.exe
exit /b 0
