@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\curve-baseline-snapshot-fake mkdir build\curve-baseline-snapshot-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\curve-baseline-snapshot-fake/" ^
  /Fd"build\curve-baseline-snapshot-fake\curve_baseline_snapshot_fake_harness.pdb" ^
  /Fe"build\curve-baseline-snapshot-fake\curve_baseline_snapshot_fake_harness.exe" ^
  tests\curve_baseline_snapshot_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBaselineSnapshot.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
