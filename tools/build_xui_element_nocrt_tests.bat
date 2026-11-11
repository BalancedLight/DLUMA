@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
call tools\setup_msvc.bat || exit /b 1
set "DLUMA_XUI_TEST_OUT=build\xui-element-nocrt"
if not exist "%DLUMA_XUI_TEST_OUT%" mkdir "%DLUMA_XUI_TEST_OUT%"
cl.exe /nologo /c /TC /O2 /Oi- /W4 /WX /Zl /GS- /fp:precise /Fo"%DLUMA_XUI_TEST_OUT%\core.obj" src\game\dlw_xui_element_core.c || exit /b 1
cl.exe /nologo /c /TC /O2 /Oi- /W4 /WX /Zl /GS- /fp:precise /Fo"%DLUMA_XUI_TEST_OUT%\test.obj" tests\xui_element_nocrt_harness.c || exit /b 1
link.exe /nologo /NODEFAULTLIB /ENTRY:entry /SUBSYSTEM:CONSOLE /MACHINE:X64 /OUT:"%DLUMA_XUI_TEST_OUT%\test.exe" "%DLUMA_XUI_TEST_OUT%\core.obj" "%DLUMA_XUI_TEST_OUT%\test.obj" kernel32.lib || exit /b 1
"%DLUMA_XUI_TEST_OUT%\test.exe" || exit /b 1
echo xui element no-CRT link passed
