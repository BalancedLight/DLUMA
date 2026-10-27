@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_EDITOR_EXIT_OUT=build\editor-exit-intent-fake"
if exist "%DLW_EDITOR_EXIT_OUT%" rmdir /s /q "%DLW_EDITOR_EXIT_OUT%" || exit /b 1
mkdir "%DLW_EDITOR_EXIT_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_EDITOR_EXIT_OUT%\dlw_editor_exit_intent.obj" ^
  src\core\dlw_editor_exit_intent.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_EDITOR_EXIT_OUT%\editor_exit_intent_fake_harness.obj" ^
  tests\editor_exit_intent_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_EDITOR_EXIT_OUT%\editor_exit_intent_fake_harness.exe" ^
  "%DLW_EDITOR_EXIT_OUT%\dlw_editor_exit_intent.obj" ^
  "%DLW_EDITOR_EXIT_OUT%\editor_exit_intent_fake_harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_EDITOR_EXIT_OUT%\editor_exit_intent_fake_harness.exe" || exit /b 1
