@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\crafting-native-gateway-fake mkdir build\crafting-native-gateway-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\crafting-native-gateway-fake/" ^
  /Fe"build\crafting-native-gateway-fake\crafting_native_gateway_fake_harness.exe" ^
  tests\crafting_native_gateway_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CraftingNativeGateway.cpp ^
  src\fixes\custom_map_asset_isolation\CraftingPreservedManager.cpp ^
  src\fixes\custom_map_asset_isolation\CraftingManagerSnapshot.cpp ^
  /link kernel32.lib || exit /b 1
exit /b 0
