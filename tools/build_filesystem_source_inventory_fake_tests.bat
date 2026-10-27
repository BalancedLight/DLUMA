@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\filesystem-source-inventory-fake mkdir build\filesystem-source-inventory-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\filesystem-source-inventory-fake/" ^
  /Fd"build\filesystem-source-inventory-fake\filesystem_source_inventory_fake_harness.pdb" ^
  /Fe"build\filesystem-source-inventory-fake\filesystem_source_inventory_fake_harness.exe" ^
  tests\filesystem_source_inventory_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\FilesystemSourceInventory.cpp ^
  src\fixes\custom_map_asset_isolation\FilesystemSourceWindowsView.cpp ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\filesystem-source-inventory-fake\filesystem_source_inventory_fake_harness.exe
exit /b 0
