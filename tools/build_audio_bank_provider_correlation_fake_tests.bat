@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\audio-bank-provider-fake mkdir build\audio-bank-provider-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\audio-bank-provider-fake/" ^
  /Fd"build\audio-bank-provider-fake\audio_bank_provider_correlation_fake_harness.pdb" ^
  /Fe"build\audio-bank-provider-fake\audio_bank_provider_correlation_fake_harness.exe" ^
  tests\audio_bank_provider_correlation_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\AudioBankProviderCorrelation.cpp ^
  /link kernel32.lib user32.lib || exit /b 1

echo Built: build\audio-bank-provider-fake\audio_bank_provider_correlation_fake_harness.exe
exit /b 0
