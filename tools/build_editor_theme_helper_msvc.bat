@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
if /I not "%~1"=="Release" if /I not "%~1"=="Debug" exit /b 1
if "%~2"=="" exit /b 1
where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
if not exist "%~2" mkdir "%~2" || exit /b 1
python tools\generate_editor_theme.py || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /W4 /WX /Fo"%~2\core.obj" src\core\dlw_editor_theme_core.c || exit /b 1
cl.exe /nologo /c /std:c++17 /EHsc /MT /O2 /W4 /WX /Fo"%~2\system.obj" tools\editor-theme\editor_theme_system.cpp || exit /b 1
link.exe /nologo /dll /machine:x64 /out:"%~2\dluma_editor_theme_system.dll" "%~2\core.obj" "%~2\system.obj" runtimeobject.lib user32.lib ole32.lib || exit /b 1
exit /b 0
