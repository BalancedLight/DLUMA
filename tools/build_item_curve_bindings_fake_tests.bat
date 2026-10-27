@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\item-curve-bindings-fake mkdir build\item-curve-bindings-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\item-curve-bindings-fake/" ^
  /Fd"build\item-curve-bindings-fake\item_curve_bindings_fake_harness.pdb" ^
  /Fe"build\item-curve-bindings-fake\item_curve_bindings_fake_harness.exe" ^
  tests\item_curve_bindings_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBaselineSnapshot.cpp ^
  src\fixes\custom_map_asset_isolation\ItemCurveBindings.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
