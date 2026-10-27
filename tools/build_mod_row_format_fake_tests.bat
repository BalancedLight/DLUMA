@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\mod-row-format-fake mkdir build\mod-row-format-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\mod-row-format-fake/" ^
  /Fd"build\mod-row-format-fake\mod_row_format_core_harness.pdb" ^
  /Fe"build\mod-row-format-fake\mod_row_format_core_harness.exe" ^
  tests\mod_row_format_core_harness.c src\dlw_mod_row_format_core.c || exit /b 1

echo Built: build\mod-row-format-fake\mod_row_format_core_harness.exe
exit /b 0
