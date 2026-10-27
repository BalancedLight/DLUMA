@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\packed-asset-owner-fake mkdir build\packed-asset-owner-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\packed-asset-owner-fake/" ^
 /Fe"build\packed-asset-owner-fake\packed_asset_owner_fake_harness.exe" ^
 tests\packed_asset_owner_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\PackedAssetOwner.cpp ^
 src\fixes\custom_map_asset_isolation\PackedAssetWindows.cpp ^
 src\fixes\custom_map_asset_isolation\NativeRetainedResourceOwner.cpp ^
 /link kernel32.lib || exit /b 1
exit /b 0
