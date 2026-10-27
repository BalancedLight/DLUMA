@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\native-fake mkdir build\native-fake

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX /DDLW_NATIVE_FAKE_TESTS /I src ^
  /Fd"build\native-fake\native_gameplay_fake_harness.pdb" ^
  /Fo"build\native-fake\native_gameplay_fake_harness.obj" ^
  tests\native_gameplay_fake_harness.c || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /W4 /WX /DDLW_NATIVE_FAKE_TESTS /I src ^
  /Fd"build\native-fake\native_gameplay_fake_harness.pdb" ^
  /Fo"build\native-fake\dlw_native_gameplay.obj" ^
  src\dlw_native_gameplay.c || exit /b 1

link.exe /nologo /machine:x64 ^
  /out:"build\native-fake\native_gameplay_fake_harness.exe" ^
  "build\native-fake\native_gameplay_fake_harness.obj" ^
  "build\native-fake\dlw_native_gameplay.obj" ^
  kernel32.lib user32.lib || exit /b 1

echo Built: build\native-fake\native_gameplay_fake_harness.exe
exit /b 0
