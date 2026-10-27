@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\detached-source-retirement-fake mkdir build\detached-source-retirement-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\detached-source-retirement-fake/" ^
  /Fd"build\detached-source-retirement-fake\detached_source_retirement_fake_harness.pdb" ^
  /Fe"build\detached-source-retirement-fake\detached_source_retirement_fake_harness.exe" ^
  tests\detached_source_retirement_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\DetachedSourceRetirement.cpp ^
  /link kernel32.lib user32.lib || exit /b 1
exit /b 0
