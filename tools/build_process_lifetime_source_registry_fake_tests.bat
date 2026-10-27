@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\process-lifetime-source-registry-fake mkdir build\process-lifetime-source-registry-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\process-lifetime-source-registry-fake/" ^
  /Fd"build\process-lifetime-source-registry-fake\process_lifetime_source_registry_fake_harness.pdb" ^
  /Fe"build\process-lifetime-source-registry-fake\process_lifetime_source_registry_fake_harness.exe" ^
  tests\process_lifetime_source_registry_fake_harness.c ^
  src\dlw_process_lifetime_sources.c ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\process-lifetime-source-registry-fake\process_lifetime_source_registry_fake_harness.exe
exit /b 0
