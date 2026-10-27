@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\startup-filesystem-source-classifier-fake mkdir build\startup-filesystem-source-classifier-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\startup-filesystem-source-classifier-fake/" ^
  /Fd"build\startup-filesystem-source-classifier-fake\startup_filesystem_source_classifier_fake_harness.pdb" ^
  /Fe"build\startup-filesystem-source-classifier-fake\startup_filesystem_source_classifier_fake_harness.exe" ^
  tests\startup_filesystem_source_classifier_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\StartupFilesystemSourceClassifier.cpp ^
  src\fixes\custom_map_asset_isolation\AudioMinimumSemanticPlan.cpp ^
  src\fixes\custom_map_asset_isolation\PlayerGameplayParamsRestartAdapter.cpp ^
  src\fixes\custom_map_asset_isolation\PlayerGameplayParamsWindowsSnapshot.cpp ^
  src\fixes\custom_map_asset_isolation\StartupBaselineCaptureState.cpp ^
  src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
  src\dlw_native_companion_source_owner.c ^
  src\dlw_process_lifetime_sources.c ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\startup-filesystem-source-classifier-fake\startup_filesystem_source_classifier_fake_harness.exe
exit /b 0
