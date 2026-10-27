@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\orderly-close-provenance-fake mkdir build\orderly-close-provenance-fake

cl.exe /nologo /TC /MT /O2 /W4 /WX ^
  /Fo"build\orderly-close-provenance-fake/" ^
  /Fd"build\orderly-close-provenance-fake\orderly_close_provenance_fake_harness.pdb" ^
  /Fe"build\orderly-close-provenance-fake\orderly_close_provenance_fake_harness.exe" ^
  tests\orderly_close_provenance_fake_harness.c ^
  src\dlw_orderly_close_core.c ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\orderly-close-provenance-fake\orderly_close_provenance_fake_harness.exe
exit /b 0
