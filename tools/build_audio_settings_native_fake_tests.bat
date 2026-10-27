@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\audio-settings-native-fake mkdir build\audio-settings-native-fake
ml64.exe /nologo /c /Fo"build\audio-settings-native-fake\mix_probe.obj" src\audio_settings_mix_probe_masm.asm || exit /b 1
ml64.exe /nologo /c /Fo"build\audio-settings-native-fake\mix_test.obj" tests\audio_settings_mix_test_masm.asm || exit /b 1
cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\audio-settings-native-fake/" ^
  /Fd"build\audio-settings-native-fake\audio_settings_native_fake_harness.pdb" ^
  /Fe"build\audio-settings-native-fake\audio_settings_native_fake_harness.exe" ^
  tests\audio_settings_native_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\AudioSettingsNative.cpp ^
  /link build\audio-settings-native-fake\mix_probe.obj build\audio-settings-native-fake\mix_test.obj kernel32.lib || exit /b 1
exit /b 0
