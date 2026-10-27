@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\item-descriptor-snapshot-fake mkdir build\item-descriptor-snapshot-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\item-descriptor-snapshot-fake/" ^
  /Fe"build\item-descriptor-snapshot-fake\item_descriptor_snapshot_fake_harness.exe" ^
  tests\item_descriptor_snapshot_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\ItemDescriptorSnapshot.cpp ^
  src\fixes\custom_map_asset_isolation\ItemAuxiliarySnapshot.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
