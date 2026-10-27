@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\candidate-retaining-provenance-fake mkdir build\candidate-retaining-provenance-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\candidate-retaining-provenance-fake/" ^
  /Fd"build\candidate-retaining-provenance-fake\candidate_retaining_consumer_provenance_fake_harness.pdb" ^
  /Fe"build\candidate-retaining-provenance-fake\candidate_retaining_consumer_provenance_fake_harness.exe" ^
  tests\candidate_retaining_consumer_provenance_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\CandidateRetainingConsumerProvenance.cpp ^
  src\fixes\custom_map_asset_isolation\ContentDomainRestartRegistry.cpp ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\candidate-retaining-provenance-fake\candidate_retaining_consumer_provenance_fake_harness.exe
exit /b 0
