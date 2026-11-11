@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\xui-element-core mkdir build\xui-element-core
cl.exe /nologo /TC /MT /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fe"build\xui-element-core\xui_element_core_fake_harness.exe" ^
  /Fo"build\xui-element-core\\" ^
  tests\xui_element_core_fake_harness.c src\game\dlw_xui_element_core.c || exit /b 1
build\xui-element-core\xui_element_core_fake_harness.exe || exit /b 1
