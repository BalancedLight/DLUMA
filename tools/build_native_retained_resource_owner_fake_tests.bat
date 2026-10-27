@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\native-retained-resource-fake mkdir build\native-retained-resource-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\native-retained-resource-fake/" ^
 /Fe"build\native-retained-resource-fake\native_retained_resource_owner_fake_harness.exe" ^
 tests\native_retained_resource_owner_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\NativeRetainedResourceOwner.cpp ^
 src\fixes\custom_map_asset_isolation\NativeRetainedResourceWindows.cpp ^
 /link kernel32.lib || exit /b 1
exit /b 0
