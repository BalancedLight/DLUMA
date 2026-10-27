@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1

set "DLW_BYPASS_BUILD=build\source-bypass-invariant-fake"
if not exist "%DLW_BYPASS_BUILD%" mkdir "%DLW_BYPASS_BUILD%" || exit /b 1
cl.exe /nologo /std:c11 /W4 /WX /EHsc- /DWIN32_LEAN_AND_MEAN ^
  /I src ^
  /Fo"%DLW_BYPASS_BUILD%\\" ^
  /Fd"%DLW_BYPASS_BUILD%\source_bypass_invariant_fake_harness.pdb" ^
  /Fe"%DLW_BYPASS_BUILD%\source_bypass_invariant_fake_harness.exe" ^
  tests\source_bypass_invariant_fake_harness.c ^
  src\dlw_project_launch_admission.c ^
  /link /incremental:no || exit /b 1
"%DLW_BYPASS_BUILD%\source_bypass_invariant_fake_harness.exe" || exit /b 1
echo Built and passed: %DLW_BYPASS_BUILD%\source_bypass_invariant_fake_harness.exe
exit /b 0
