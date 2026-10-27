@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if exist build\vehicle-gate-fake rmdir /s /q build\vehicle-gate-fake || exit /b 1
mkdir build\vehicle-gate-fake || exit /b 1

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fe"build\vehicle-gate-fake\vehicle_gate_fake_harness.exe" ^
  /Fo"build\vehicle-gate-fake\vehicle_gate_fake_harness.obj" ^
  tests\vehicle_gate_fake_harness.c || exit /b 1

build\vehicle-gate-fake\vehicle_gate_fake_harness.exe || exit /b 1
echo Built and passed: build\vehicle-gate-fake\vehicle_gate_fake_harness.exe
exit /b 0
