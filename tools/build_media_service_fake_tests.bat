@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if exist build\media-service-fake rmdir /s /q build\media-service-fake || exit /b 1
mkdir build\media-service-fake || exit /b 1

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fe"build\media-service-fake\media_service_fake_harness.exe" ^
  /Fo"build\media-service-fake\media_service_fake_harness.obj" ^
  tests\media_service_fake_harness.c /link kernel32.lib || exit /b 1

build\media-service-fake\media_service_fake_harness.exe || exit /b 1
echo Built and passed: build\media-service-fake\media_service_fake_harness.exe
exit /b 0
