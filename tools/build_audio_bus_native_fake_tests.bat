@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-bus-native-fake mkdir build\audio-bus-native-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-bus-native-fake/" ^
 /Fe"build\audio-bus-native-fake\audio_bus_native_fake_harness.exe" ^
 tests\audio_bus_native_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioBusNative.cpp || exit /b 1
exit /b 0
