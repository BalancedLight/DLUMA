@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\content-rollback-fake mkdir build\content-rollback-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\content-rollback-fake/" ^
  /Fd"build\content-rollback-fake\custom_content_rollback_fake_harness.pdb" ^
  /Fe"build\content-rollback-fake\custom_content_rollback_fake_harness.exe" ^
  tests\custom_content_rollback_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\FrontendSourceRollbackLifecycle.cpp ^
  src\fixes\custom_map_asset_isolation\CustomContentLedger.cpp ^
  src\fixes\custom_map_asset_isolation\CancelledPresentationCleanupVerifier.cpp ^
  src\fixes\custom_map_asset_isolation\CustomContentResetRegistry.cpp ^
  src\fixes\custom_map_asset_isolation\CustomAudioBankLedger.cpp ^
  src\fixes\custom_map_asset_isolation\CustomAudioResetAdapter.cpp ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\content-rollback-fake\custom_content_rollback_fake_harness.exe
exit /b 0
