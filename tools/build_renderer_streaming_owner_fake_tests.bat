@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist build\renderer-streaming-owner-fake mkdir build\renderer-streaming-owner-fake
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
 /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
 /Fo"build\renderer-streaming-owner-fake/" ^
 /Fe"build\renderer-streaming-owner-fake\renderer_streaming_owner_fake_harness.exe" ^
 tests\renderer_streaming_owner_fake_harness.cpp ^
 tests\renderer_relay_unwind_windows.cpp ^
 src\fixes\custom_map_asset_isolation\RendererStreamingOwner.cpp ^
 src\fixes\custom_map_asset_isolation\PackedAssetOwner.cpp ^
 src\fixes\custom_map_asset_isolation\NativeRetainedResourceOwner.cpp ^
 /link /stack:16777216 kernel32.lib user32.lib || exit /b 1
exit /b 0
