@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\frontend-cold-reboot-fake mkdir build\frontend-cold-reboot-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\frontend-cold-reboot-fake/" ^
  /Fd"build\frontend-cold-reboot-fake\frontend_content_cold_reboot_fake_harness.pdb" ^
  /Fe"build\frontend-cold-reboot-fake\frontend_content_cold_reboot_fake_harness.exe" ^
  tests\frontend_content_cold_reboot_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
  src\fixes\custom_map_asset_isolation\FrontendBaseline.cpp ^
  src\fixes\custom_map_asset_isolation\FrontendContentColdReboot.cpp ^
  src\fixes\custom_map_asset_isolation\StateFiveDestroyOwner.cpp ^
  src\fixes\custom_map_asset_isolation\FrontendColdRebootWindowsProfile.cpp ^
  src\fixes\custom_map_asset_isolation\FrontendSourceRollbackLifecycle.cpp ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\frontend-cold-reboot-fake\frontend_content_cold_reboot_fake_harness.exe
exit /b 0
