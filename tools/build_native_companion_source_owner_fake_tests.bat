@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\native-companion-source-owner-fake mkdir build\native-companion-source-owner-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\native-companion-source-owner-fake/" ^
  /Fd"build\native-companion-source-owner-fake\native_companion_source_owner_fake_harness.pdb" ^
  /Fe"build\native-companion-source-owner-fake\native_companion_source_owner_fake_harness.exe" ^
  tests\native_companion_source_owner_fake_harness.c ^
  src\dlw_native_companion_source_owner.c ^
  src\dlw_process_lifetime_sources.c ^
  /link kernel32.lib user32.lib || exit /b 1

"build\native-companion-source-owner-fake\native_companion_source_owner_fake_harness.exe" || exit /b 1
echo Built and passed: build\native-companion-source-owner-fake\native_companion_source_owner_fake_harness.exe
exit /b 0
