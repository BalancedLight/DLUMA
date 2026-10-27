@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\project-owner-identity-fake mkdir build\project-owner-identity-fake

cl.exe /nologo /c /TC /MT /O2 /Gy /W4 /WX /I src ^
  /Fo"build\project-owner-identity-fake\dlw_project_owner_identity.obj" ^
  src\dlw_project_owner_identity.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Gy /W4 /WX /I src ^
  /Fo"build\project-owner-identity-fake\project_owner_identity_fake_harness.obj" ^
  tests\project_owner_identity_fake_harness.c || exit /b 1
link.exe /nologo /subsystem:console /opt:ref ^
  /out:"build\project-owner-identity-fake\project_owner_identity_fake_harness.exe" ^
  "build\project-owner-identity-fake\project_owner_identity_fake_harness.obj" ^
  "build\project-owner-identity-fake\dlw_project_owner_identity.obj" ^
  kernel32.lib libcmt.lib libvcruntime.lib || exit /b 1

echo Built: build\project-owner-identity-fake\project_owner_identity_fake_harness.exe
exit /b 0
