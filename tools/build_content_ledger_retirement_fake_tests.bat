@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\content-retirement-fake mkdir build\content-retirement-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\content-retirement-fake/" /Fe"build\content-retirement-fake\content_retirement.exe" ^
  tests\content_ledger_retirement_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CustomContentLedger.cpp ^
  /link kernel32.lib user32.lib || exit /b 1
build\content-retirement-fake\content_retirement.exe || exit /b 1
exit /b 0
