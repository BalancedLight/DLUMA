@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\native-file-lifetime-fake mkdir build\native-file-lifetime-fake
ml64.exe /nologo /c /Fo"build\native-file-lifetime-fake\native_file_completion_probe.obj" src\native_file_completion_probe_masm.asm || exit /b 1
ml64.exe /nologo /c /Fo"build\native-file-lifetime-fake\native_file_lifetime_test.obj" tests\native_file_lifetime_test_masm.asm || exit /b 1
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\native-file-lifetime-fake/" ^
 /Fd"build\native-file-lifetime-fake\native_file_lifetime_fake_harness.pdb" ^
 /Fe"build\native-file-lifetime-fake\native_file_lifetime_fake_harness.exe" ^
 tests\native_file_lifetime_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileLifetimeLedger.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileLifetimeWindows.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileHookBridge.cpp ^
 src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
 /link /stack:16777216 kernel32.lib user32.lib ^
 build\native-file-lifetime-fake\native_file_completion_probe.obj ^
 build\native-file-lifetime-fake\native_file_lifetime_test.obj || exit /b 1
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\native-file-lifetime-fake/" /Fe"build\native-file-lifetime-fake\native_file_bridge_fake_harness.exe" ^
 tests\native_file_bridge_fake_harness.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileLifetimeLedger.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileLifetimeWindows.cpp ^
 src\fixes\custom_map_asset_isolation\NativeFileHookBridge.cpp ^
 src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
 /link /stack:16777216 kernel32.lib user32.lib ^
 build\native-file-lifetime-fake\native_file_completion_probe.obj || exit /b 1
build\native-file-lifetime-fake\native_file_bridge_fake_harness.exe || exit /b 1
exit /b 0
