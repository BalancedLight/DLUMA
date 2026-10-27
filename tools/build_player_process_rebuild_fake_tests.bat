@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\player-process-rebuild-fake mkdir build\player-process-rebuild-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\player-process-rebuild-fake/" ^
 /Fd"build\player-process-rebuild-fake\player_process_rebuild_fake_harness.pdb" ^
 /Fe"build\player-process-rebuild-fake\player_process_rebuild_fake_harness.exe" ^
 tests\player_process_rebuild_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\PlayerProcessOwnerRebuild.cpp ^
 /link /stack:8388608 kernel32.lib user32.lib || exit /b 1
exit /b 0
