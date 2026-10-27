@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLW_LAUNCH_BUILD=build\project-launch-admission-fake"
if exist "%DLW_LAUNCH_BUILD%" rmdir /s /q "%DLW_LAUNCH_BUILD%" || exit /b 1
mkdir "%DLW_LAUNCH_BUILD%" || exit /b 1

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"%DLW_LAUNCH_BUILD%\\" ^
  /Fd"%DLW_LAUNCH_BUILD%\project_launch_admission_fake_harness.pdb" ^
  /Fe"%DLW_LAUNCH_BUILD%\project_launch_admission_fake_harness.exe" ^
  tests\project_launch_admission_fake_harness.c ^
  src\dlw_project_launch_admission.c ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

"%DLW_LAUNCH_BUILD%\project_launch_admission_fake_harness.exe" || exit /b 1
echo Built and passed: %DLW_LAUNCH_BUILD%\project_launch_admission_fake_harness.exe
exit /b 0
