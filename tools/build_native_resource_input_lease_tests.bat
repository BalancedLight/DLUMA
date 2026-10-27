@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\native-resource-input-lease mkdir build\native-resource-input-lease
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\native-resource-input-lease/" ^
 /Fe"build\native-resource-input-lease\native_resource_input_lease_harness.exe" ^
 tests\native_resource_input_lease_harness.cpp ^
 src\fixes\custom_map_asset_isolation\NativeResourceInputLease.cpp ^
 /Tcsrc\dlw_sha256.c /link /stack:16777216 kernel32.lib user32.lib || exit /b 1
exit /b 0
