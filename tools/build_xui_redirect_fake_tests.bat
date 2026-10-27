@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\xui-redirect-fake mkdir build\xui-redirect-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"build\xui-redirect-fake\xui_redirect_fake_harness.obj" ^
  /Fd"build\xui-redirect-fake\xui_redirect_fake_harness.pdb" ^
  /Fe"build\xui-redirect-fake\xui_redirect_fake_harness.exe" ^
  tests\xui_redirect_fake_harness.c || exit /b 1

echo Built: build\xui-redirect-fake\xui_redirect_fake_harness.exe
exit /b 0
