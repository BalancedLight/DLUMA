@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\workshop-enrichment-fake mkdir build\workshop-enrichment-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX ^
  /Fo"build\workshop-enrichment-fake\workshop_enrichment_fake_harness.obj" ^
  /Fd"build\workshop-enrichment-fake\workshop_enrichment_fake_harness.pdb" ^
  /Fe"build\workshop-enrichment-fake\workshop_enrichment_fake_harness.exe" ^
  tests\workshop_enrichment_fake_harness.c || exit /b 1

echo Built: build\workshop-enrichment-fake\workshop_enrichment_fake_harness.exe
exit /b 0
