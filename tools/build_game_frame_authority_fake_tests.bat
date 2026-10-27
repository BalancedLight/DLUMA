@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLW_AUTHORITY_BUILD=build\game-frame-authority-fake"
if exist "%DLW_AUTHORITY_BUILD%" rmdir /s /q "%DLW_AUTHORITY_BUILD%" || exit /b 1
mkdir "%DLW_AUTHORITY_BUILD%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"%DLW_AUTHORITY_BUILD%\dlw_game_frame_authority.obj" ^
  src\dlw_game_frame_authority.c || exit /b 1

cl.exe /nologo /c /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- ^
  /std:c++17 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"%DLW_AUTHORITY_BUILD%\startup_baseline_capture_state.obj" ^
  src\fixes\custom_map_asset_isolation\StartupBaselineCaptureState.cpp || exit /b 1

cl.exe /nologo /c /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- ^
  /std:c++17 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"%DLW_AUTHORITY_BUILD%\game_frame_authority_fake_harness.obj" ^
  tests\game_frame_authority_fake_harness.cpp || exit /b 1

link.exe /nologo /machine:x64 ^
  /out:"%DLW_AUTHORITY_BUILD%\game_frame_authority_fake_harness.exe" ^
  "%DLW_AUTHORITY_BUILD%\game_frame_authority_fake_harness.obj" ^
  "%DLW_AUTHORITY_BUILD%\dlw_game_frame_authority.obj" ^
  "%DLW_AUTHORITY_BUILD%\startup_baseline_capture_state.obj" ^
  kernel32.lib user32.lib || exit /b 1

"%DLW_AUTHORITY_BUILD%\game_frame_authority_fake_harness.exe" || exit /b 1
echo Built and passed: %DLW_AUTHORITY_BUILD%\game_frame_authority_fake_harness.exe
exit /b 0
