@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\player-preset-row-publication-timeline-fake mkdir build\player-preset-row-publication-timeline-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\player-preset-row-publication-timeline-fake/" ^
  /Fd"build\player-preset-row-publication-timeline-fake\player_preset_row_publication_timeline_fake_harness.pdb" ^
  /Fe"build\player-preset-row-publication-timeline-fake\player_preset_row_publication_timeline_fake_harness.exe" ^
  tests\player_preset_row_publication_timeline_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\PlayerPresetRowPublicationTimeline.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\player-preset-row-publication-timeline-fake\player_preset_row_publication_timeline_fake_harness.exe
exit /b 0
