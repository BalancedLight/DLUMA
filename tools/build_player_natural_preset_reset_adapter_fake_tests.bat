@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "OUT=build\player_natural_preset_reset_adapter_fake_tests"
if not exist "%OUT%" mkdir "%OUT%"

cl.exe /nologo /TP /MT /std:c++17 /EHs-c- /GR- /W4 /WX /Od /Zi ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS ^
  /I src ^
  tests\player_natural_preset_reset_adapter_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\PlayerNaturalPresetResetAdapter.cpp ^
  /Fe:"%OUT%\player_natural_preset_reset_adapter_fake_harness.exe" ^
  /Fo"%OUT%\\" /Fd"%OUT%\\" /link /incremental:no
if errorlevel 1 exit /b 1

"%OUT%\player_natural_preset_reset_adapter_fake_harness.exe"
exit /b %errorlevel%
