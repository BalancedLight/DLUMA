@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\workshop-menu-fake mkdir build\workshop-menu-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"build\workshop-menu-fake\workshop_menu_metadata_fake_harness.obj" ^
  /Fd"build\workshop-menu-fake\workshop_menu_metadata_fake_harness.pdb" ^
  /Fe"build\workshop-menu-fake\workshop_menu_metadata_fake_harness.exe" ^
  tests\workshop_menu_metadata_fake_harness.c || exit /b 1

echo Built: build\workshop-menu-fake\workshop_menu_metadata_fake_harness.exe
exit /b 0
