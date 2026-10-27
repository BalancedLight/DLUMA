@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\player-xml-input-fake mkdir build\player-xml-input-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\player-xml-input-fake/" ^
 /Fe"build\player-xml-input-fake\player_xml_input_digest_fake_harness.exe" ^
 tests\player_xml_input_digest_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\PlayerXmlInputDigest.cpp ^
 src\fixes\custom_map_asset_isolation\PlayerStateObservationLedger.cpp ^
 /Tcsrc\dlw_sha256.c ^
 /link kernel32.lib user32.lib || exit /b 1
exit /b 0
