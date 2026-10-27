@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_QP_OUT=build\quest-phase-fake"
if exist "%DLW_QP_OUT%" rmdir /s /q "%DLW_QP_OUT%" || exit /b 1
mkdir "%DLW_QP_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_QP_OUT%\dlw_quest_phase.obj" ^
  src\core\dlw_quest_phase.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_QP_OUT%\quest_phase_fake_harness.obj" ^
  tests\quest_phase_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_QP_OUT%\quest_phase_fake_harness.exe" ^
  "%DLW_QP_OUT%\dlw_quest_phase.obj" ^
  "%DLW_QP_OUT%\quest_phase_fake_harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_QP_OUT%\quest_phase_fake_harness.exe" || exit /b 1
