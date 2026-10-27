@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if exist build\native-mod-config-fake rmdir /s /q build\native-mod-config-fake || exit /b 1
mkdir build\native-mod-config-fake || exit /b 1

set "DLW_CONFIG_TEST_FLAGS=/nologo /c /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src"

cl.exe %DLW_CONFIG_TEST_FLAGS% ^
  /Fo"build\native-mod-config-fake\native_mod_config_fake_harness.obj" ^
  tests\native_mod_config_fake_harness.c || exit /b 1
cl.exe %DLW_CONFIG_TEST_FLAGS% ^
  /Fo"build\native-mod-config-fake\dlw_native_mod_config.obj" ^
  src\dlw_native_mod_config.c || exit /b 1

link.exe /nologo /machine:x64 ^
  /out:"build\native-mod-config-fake\native_mod_config_fake_harness.exe" ^
  "build\native-mod-config-fake\native_mod_config_fake_harness.obj" ^
  "build\native-mod-config-fake\dlw_native_mod_config.obj" ^
  kernel32.lib || exit /b 1

build\native-mod-config-fake\native_mod_config_fake_harness.exe || exit /b 1
echo Built and passed: build\native-mod-config-fake\native_mod_config_fake_harness.exe
exit /b 0
