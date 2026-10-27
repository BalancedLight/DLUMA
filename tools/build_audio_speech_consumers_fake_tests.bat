@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-speech-consumers-fake mkdir build\audio-speech-consumers-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-speech-consumers-fake/" ^
 /Fe"build\audio-speech-consumers-fake\audio_speech_consumers_fake_harness.exe" ^
 tests\audio_speech_consumers_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioSpeechConsumersNative.cpp || exit /b 1
exit /b 0
