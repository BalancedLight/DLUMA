@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\custom-main-menu-return-source-fake mkdir build\custom-main-menu-return-source-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\custom-main-menu-return-source-fake/" ^
  /Fd"build\custom-main-menu-return-source-fake\custom_main_menu_return_source_fake_harness.pdb" ^
  /Fe"build\custom-main-menu-return-source-fake\custom_main_menu_return_source_fake_harness.exe" ^
  tests\custom_main_menu_return_source_fake_harness.c ^
  src\dlw_process_lifetime_sources.c ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\custom-main-menu-return-source-fake\custom_main_menu_return_source_fake_harness.exe
exit /b 0
