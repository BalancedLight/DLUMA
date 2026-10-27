@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\material-baseline-owner-fake mkdir build\material-baseline-owner-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\material-baseline-owner-fake/" ^
 /Fe"build\material-baseline-owner-fake\material_baseline_owner_fake_harness.exe" ^
 tests\material_baseline_owner_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\MaterialBaselineOwner.cpp ^
 src\fixes\custom_map_asset_isolation\NativeResourceGateway.cpp ^
 src\fixes\custom_map_asset_isolation\ResourceProviderScope.cpp ^
 src\fixes\custom_map_asset_isolation\AudioBankProviderCorrelation.cpp ^
 src\fixes\custom_map_asset_isolation\CurveBorrowBoundary.cpp ^
 src\fixes\custom_map_asset_isolation\NativeRetainedResourceOwner.cpp ^
 src\fixes\custom_map_asset_isolation\PackedAssetOwner.cpp ^
 /Tcsrc\dlw_sha256.c /link /stack:16777216 kernel32.lib user32.lib || exit /b 1
exit /b 0
