@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-script-input-digest mkdir build\audio-script-input-digest
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-script-input-digest/" ^
 /Fe"build\audio-script-input-digest\audio_script_input_digest_harness.exe" ^
 tests\audio_script_input_digest_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioScriptInputDigest.cpp ^
 /Tcsrc\dlw_sha256.c /link kernel32.lib user32.lib || exit /b 1
exit /b 0
