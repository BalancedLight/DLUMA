@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLW_DISCORD_HELPER_FAKE=build\discord-presence-helper-fake"
if exist "%DLW_DISCORD_HELPER_FAKE%" rmdir /s /q "%DLW_DISCORD_HELPER_FAKE%" || exit /b 1
mkdir "%DLW_DISCORD_HELPER_FAKE%" || exit /b 1

cl.exe /nologo /c /TP /std:c++20 /EHsc /MT /O2 /W4 /WX ^
  /I"src" ^
  /DDLW_DISCORD_PRESENCE_HELPER_EXPORTS ^
  /DDLW_DISCORD_RPC_TEST_PIPE ^
  /Fo"%DLW_DISCORD_HELPER_FAKE%\dlw_discord_presence_helper.obj" ^
  /Fd"%DLW_DISCORD_HELPER_FAKE%\dlw_discord_presence_helper.pdb" ^
  src\dlw_discord_presence_helper.cpp || exit /b 1

cl.exe /nologo /c /TP /std:c++20 /EHsc /MT /O2 /W4 /WX ^
  /I"src" ^
  /Fo"%DLW_DISCORD_HELPER_FAKE%\discord_presence_helper_fake_harness.obj" ^
  /Fd"%DLW_DISCORD_HELPER_FAKE%\discord_presence_helper_fake_harness.pdb" ^
  tests\discord_presence_helper_fake_harness.cpp || exit /b 1

link.exe /nologo ^
  /OUT:"%DLW_DISCORD_HELPER_FAKE%\discord_presence_helper_fake_harness.exe" ^
  "%DLW_DISCORD_HELPER_FAKE%\dlw_discord_presence_helper.obj" ^
  "%DLW_DISCORD_HELPER_FAKE%\discord_presence_helper_fake_harness.obj" || exit /b 1

echo Built: %DLW_DISCORD_HELPER_FAKE%\discord_presence_helper_fake_harness.exe
exit /b 0
