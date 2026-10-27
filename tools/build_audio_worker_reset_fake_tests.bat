@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\audio-worker-reset-fake mkdir build\audio-worker-reset-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\audio-worker-reset-fake/" ^
 /Fd"build\audio-worker-reset-fake\audio_worker_reset_fake_harness.pdb" ^
 /Fe"build\audio-worker-reset-fake\audio_worker_reset_fake_harness.exe" ^
 tests\audio_worker_reset_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\AudioWorkerReset.cpp ^
 src\fixes\custom_map_asset_isolation\AudioWorkerResetWindows.cpp ^
 /link /stack:8388608 kernel32.lib user32.lib || exit /b 1
exit /b 0
