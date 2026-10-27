@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-baseline-content mkdir build\audio-baseline-content
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-baseline-content/" ^
 /Fd"build\audio-baseline-content\audio_baseline_content_harness.pdb" ^
 /Fe"build\audio-baseline-content\audio_baseline_content_harness.exe" ^
 tests\audio_baseline_content_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioBaselineContent.cpp ^
 src\fixes\custom_map_asset_isolation\AudioStartupObservationLedger.cpp ^
 src\fixes\custom_map_asset_isolation\SpeechContentLease.cpp ^
 /Tcsrc\dlw_sha256.c ^
 /link /stack:8388608 kernel32.lib user32.lib || exit /b 1
exit /b 0
