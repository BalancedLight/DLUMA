@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\hud-resolver-gate mkdir build\hud-resolver-gate
cl.exe /nologo /TC /MT /W4 /WX /Fe"build\hud-resolver-gate\test.exe" ^
  /Fo"build\hud-resolver-gate\test.obj" tests\hud_resolver_gate_fake_harness.c || exit /b 1
build\hud-resolver-gate\test.exe || exit /b 1
