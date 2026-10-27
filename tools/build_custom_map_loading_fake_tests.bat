@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\custom-map-loading-fake mkdir build\custom-map-loading-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"build\custom-map-loading-fake\custom_map_loading_fake_harness.obj" ^
  /Fd"build\custom-map-loading-fake\custom_map_loading_fake_harness.pdb" ^
  /Fe"build\custom-map-loading-fake\custom_map_loading_fake_harness.exe" ^
  tests\custom_map_loading_fake_harness.c || exit /b 1

echo Built: build\custom-map-loading-fake\custom_map_loading_fake_harness.exe
exit /b 0
