@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\post-destroy-preflight-report-fake mkdir build\post-destroy-preflight-report-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\post-destroy-preflight-report-fake/" ^
  /Fd"build\post-destroy-preflight-report-fake\post_destroy_preflight_report_fake_harness.pdb" ^
  /Fe"build\post-destroy-preflight-report-fake\post_destroy_preflight_report_fake_harness.exe" ^
  tests\post_destroy_preflight_report_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\PostDestroyPreflightReport.cpp ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\post-destroy-preflight-report-fake\post_destroy_preflight_report_fake_harness.exe
exit /b 0
