@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\startup-baseline-capture-state-fake mkdir build\startup-baseline-capture-state-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\startup-baseline-capture-state-fake/" ^
  /Fd"build\startup-baseline-capture-state-fake\startup_baseline_capture_state_fake_harness.pdb" ^
  /Fe"build\startup-baseline-capture-state-fake\startup_baseline_capture_state_fake_harness.exe" ^
  tests\startup_baseline_capture_state_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\StartupBaselineCaptureState.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\startup-baseline-capture-state-fake\startup_baseline_capture_state_fake_harness.exe
exit /b 0
