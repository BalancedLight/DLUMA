@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\build-identity-fake mkdir build\build-identity-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\build-identity-fake/" ^
  /Fd"build\build-identity-fake\build_identity_fake_harness.pdb" ^
  /Fe"build\build-identity-fake\build_identity_fake_harness.exe" ^
  tests\build_identity_fake_harness.c src\dlw_build_identity_core.c || exit /b 1

echo Built: build\build-identity-fake\build_identity_fake_harness.exe
exit /b 0
