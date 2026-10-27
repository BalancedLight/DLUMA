@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\content-replay-permit-fake mkdir build\content-replay-permit-fake
python -B tools\prepare_retaining_observer_fake.py || exit /b 1
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHsc /GR- /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I src /I build\content-replay-permit-fake ^
 /Fo"build\content-replay-permit-fake/" ^
 /Fe"build\content-replay-permit-fake\content_replay_permit_fake_harness.exe" ^
 tests\content_replay_permit_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileLifetimeLedger.cpp ^
 /link kernel32.lib user32.lib || exit /b 1
exit /b 0
