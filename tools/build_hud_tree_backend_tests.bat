@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
set "DLUMA_HUD_TEST_OUT=build\hud-tree-tests"
if not exist "%DLUMA_HUD_TEST_OUT%" mkdir "%DLUMA_HUD_TEST_OUT%" || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /Gw /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
 /Fo"%DLUMA_HUD_TEST_OUT%\hud_tree.obj" tests\hud_tree_backend_fake_harness.c ^
 /Fe"%DLUMA_HUD_TEST_OUT%\hud_tree.exe" /link /OPT:REF user32.lib || exit /b 1
"%DLUMA_HUD_TEST_OUT%\hud_tree.exe" || exit /b 1
