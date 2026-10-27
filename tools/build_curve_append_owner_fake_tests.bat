@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\curve-append-owner-fake mkdir build\curve-append-owner-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\curve-append-owner-fake/" ^
  /Fd"build\curve-append-owner-fake\curve_append_owner_fake_harness.pdb" ^
  /Fe"build\curve-append-owner-fake\curve_append_owner_fake_harness.exe" ^
  tests\curve_append_owner_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CurveAppendOnlyOwner.cpp ^
  src\fixes\custom_map_asset_isolation\CurveNativeGateway.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBaselineSnapshot.cpp ^
  src\fixes\custom_map_asset_isolation\CurveProcessCaches.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBorrowBoundary.cpp ^
  src\fixes\custom_map_asset_isolation\CurveLoadOrigin.cpp ^
  src\fixes\custom_map_asset_isolation\ItemCurveBindings.cpp ^
  /link kernel32.lib ntdll.lib || exit /b 1
exit /b 0
