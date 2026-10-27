@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\curve-borrow-boundary-fake mkdir build\curve-borrow-boundary-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\curve-borrow-boundary-fake/" ^
  /Fd"build\curve-borrow-boundary-fake\curve_borrow_boundary_fake_harness.pdb" ^
  /Fe"build\curve-borrow-boundary-fake\curve_borrow_boundary_fake_harness.exe" ^
  tests\curve_borrow_boundary_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CurveBorrowBoundary.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
