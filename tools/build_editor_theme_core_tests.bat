@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
python tools\generate_editor_theme.py || exit /b 1
if not exist build\editor-theme-core-tests mkdir build\editor-theme-core-tests || exit /b 1
cl.exe /nologo /TC /MT /W4 /WX /Febuild\editor-theme-core-tests\editor_theme_core_harness.exe /Fobuild\editor-theme-core-tests\ src\core\dlw_editor_theme_core.c tests\editor_theme_core_harness.c || exit /b 1
build\editor-theme-core-tests\editor_theme_core_harness.exe
exit /b %errorlevel%
