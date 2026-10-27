@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\speech-content-lease mkdir build\speech-content-lease
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\speech-content-lease/" ^
 /Fd"build\speech-content-lease\speech_content_lease_harness.pdb" ^
 /Fe"build\speech-content-lease\speech_content_lease_harness.exe" ^
 tests\speech_content_lease_harness.cpp ^
 src\fixes\custom_map_asset_isolation\SpeechContentLease.cpp ^
 /Tcsrc\dlw_sha256.c ^
 /link /stack:8388608 kernel32.lib user32.lib || exit /b 1
exit /b 0
