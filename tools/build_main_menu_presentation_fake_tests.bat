@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\main-menu-presentation-fake mkdir build\main-menu-presentation-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\main-menu-presentation-fake/" ^
  /Fd"build\main-menu-presentation-fake\main_menu_presentation_core_harness.pdb" ^
  /Fe"build\main-menu-presentation-fake\main_menu_presentation_core_harness.exe" ^
  tests\main_menu_presentation_core_harness.c src\dlw_main_menu_presentation_core.c || exit /b 1

echo Built: build\main-menu-presentation-fake\main_menu_presentation_core_harness.exe
exit /b 0
