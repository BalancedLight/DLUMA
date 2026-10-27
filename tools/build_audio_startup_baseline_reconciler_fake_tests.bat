@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\audio-startup-baseline-reconciler-fake mkdir build\audio-startup-baseline-reconciler-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\audio-startup-baseline-reconciler-fake/" ^
  /Fd"build\audio-startup-baseline-reconciler-fake\audio_startup_baseline_reconciler_fake_harness.pdb" ^
  /Fe"build\audio-startup-baseline-reconciler-fake\audio_startup_baseline_reconciler_fake_harness.exe" ^
  tests\audio_startup_baseline_reconciler_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\AudioStartupBaselineReconciler.cpp ^
  src\fixes\custom_map_asset_isolation\AudioStartupObservationLedger.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\audio-startup-baseline-reconciler-fake\audio_startup_baseline_reconciler_fake_harness.exe
exit /b 0
