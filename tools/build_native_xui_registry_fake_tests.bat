@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\native-xui-registry-fake mkdir build\native-xui-registry-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\native-xui-registry-fake/" ^
  /Fd"build\native-xui-registry-fake\native_xui_registry_fake_harness.pdb" ^
  /Fe"build\native-xui-registry-fake\native_xui_registry_fake_harness.exe" ^
  tests\native_xui_registry_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\NativeXuiRegistrySnapshot.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
