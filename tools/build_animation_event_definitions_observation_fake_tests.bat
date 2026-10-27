@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\animation-event-definitions-observation-fake mkdir build\animation-event-definitions-observation-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\animation-event-definitions-observation-fake/" ^
  /Fd"build\animation-event-definitions-observation-fake\animation_event_definitions_observation_fake_harness.pdb" ^
  /Fe"build\animation-event-definitions-observation-fake\animation_event_definitions_observation_fake_harness.exe" ^
  tests\animation_event_definitions_observation_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\AnimationEventDefinitionsObservation.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\animation-event-definitions-observation-fake\animation_event_definitions_observation_fake_harness.exe
exit /b 0
