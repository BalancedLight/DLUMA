@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-sound-property-fake mkdir build\audio-sound-property-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHsc /GR- /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-sound-property-fake/" ^
 /Fe"build\audio-sound-property-fake\audio_sound_property_snapshot_fake_harness.exe" ^
 tests\audio_sound_property_snapshot_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioSoundPropertySnapshot.cpp || exit /b 1
exit /b 0
