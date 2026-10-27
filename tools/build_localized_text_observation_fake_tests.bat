@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\localized-text-observation-fake mkdir build\localized-text-observation-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\localized-text-observation-fake/" ^
  /Fd"build\localized-text-observation-fake\localized_text_observation_fake_harness.pdb" ^
  /Fe"build\localized-text-observation-fake\localized_text_observation_fake_harness.exe" ^
  tests\localized_text_observation_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\LocalizedTextBaselineOwner.cpp ^
  src\fixes\custom_map_asset_isolation\LocalizedTextObservationGate.cpp ^
  /link kernel32.lib user32.lib || exit /b 1
exit /b 0
