@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\workshop-refresh-fake mkdir build\workshop-refresh-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"build\workshop-refresh-fake\workshop_refresh_fake_harness.obj" ^
  /Fd"build\workshop-refresh-fake\workshop_refresh_fake_harness.pdb" ^
  /Fe"build\workshop-refresh-fake\workshop_refresh_fake_harness.exe" ^
  tests\workshop_refresh_fake_harness.c || exit /b 1

echo Built: build\workshop-refresh-fake\workshop_refresh_fake_harness.exe
exit /b 0
