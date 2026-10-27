@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
set "DLW_ANIMATION_REFRESH_OUT=build\animation-refresh-fake"
if exist "%DLW_ANIMATION_REFRESH_OUT%" rmdir /s /q "%DLW_ANIMATION_REFRESH_OUT%" || exit /b 1
mkdir "%DLW_ANIMATION_REFRESH_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_ANIMATION_REFRESH_OUT%\refresh.obj" ^
  src\core\dlw_animation_refresh.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_ANIMATION_REFRESH_OUT%\json.obj" ^
  src\core\dlw_json_reader.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_ANIMATION_REFRESH_OUT%\harness.obj" ^
  tests\animation_refresh_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_ANIMATION_REFRESH_OUT%\animation_refresh_fake_harness.exe" ^
  "%DLW_ANIMATION_REFRESH_OUT%\refresh.obj" ^
  "%DLW_ANIMATION_REFRESH_OUT%\json.obj" ^
  "%DLW_ANIMATION_REFRESH_OUT%\harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_ANIMATION_REFRESH_OUT%\animation_refresh_fake_harness.exe" || exit /b 1
