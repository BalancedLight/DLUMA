@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\audio-baseline-windows-snapshot-fake mkdir build\audio-baseline-windows-snapshot-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\audio-baseline-windows-snapshot-fake/" ^
  /Fd"build\audio-baseline-windows-snapshot-fake\audio_baseline_windows_snapshot_fake_harness.pdb" ^
  /Fe"build\audio-baseline-windows-snapshot-fake\audio_baseline_windows_snapshot_fake_harness.exe" ^
  tests\audio_baseline_windows_snapshot_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\AudioBaselineWindowsSnapshot.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\audio-baseline-windows-snapshot-fake\audio_baseline_windows_snapshot_fake_harness.exe
exit /b 0
