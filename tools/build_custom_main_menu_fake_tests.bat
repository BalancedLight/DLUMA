@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_CMM_OUT=build\custom-main-menu-fake"
if exist "%DLW_CMM_OUT%" rmdir /s /q "%DLW_CMM_OUT%" || exit /b 1
mkdir "%DLW_CMM_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /I src /Fo"%DLW_CMM_OUT%\dlw_json_reader.obj" ^
  src\core\dlw_json_reader.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /I src /Fo"%DLW_CMM_OUT%\dlw_custom_main_menu_core.obj" ^
  src\dlw_custom_main_menu_core.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /I src /Fo"%DLW_CMM_OUT%\dlw_custom_main_menu_runtime_core.obj" ^
  src\dlw_custom_main_menu_runtime_core.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /I src /Fo"%DLW_CMM_OUT%\custom_main_menu_core_fake_harness.obj" ^
  tests\custom_main_menu_core_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_CMM_OUT%\custom_main_menu_core_fake_harness.exe" ^
  "%DLW_CMM_OUT%\dlw_json_reader.obj" ^
  "%DLW_CMM_OUT%\dlw_custom_main_menu_core.obj" ^
  "%DLW_CMM_OUT%\dlw_custom_main_menu_runtime_core.obj" ^
  "%DLW_CMM_OUT%\custom_main_menu_core_fake_harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_CMM_OUT%\custom_main_menu_core_fake_harness.exe" || exit /b 1
exit /b 0
