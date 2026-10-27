@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_DUAL_HOST_OUT=build\dual-host-fake"
if exist "%DLW_DUAL_HOST_OUT%" rmdir /s /q "%DLW_DUAL_HOST_OUT%" || exit /b 1
mkdir "%DLW_DUAL_HOST_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_DUAL_HOST_OUT%\dlw_dual_host.obj" ^
  src\core\dlw_dual_host.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_DUAL_HOST_OUT%\dual_host_fake_harness.obj" ^
  tests\dual_host_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_DUAL_HOST_OUT%\dual_host_fake_harness.exe" ^
  "%DLW_DUAL_HOST_OUT%\dlw_dual_host.obj" ^
  "%DLW_DUAL_HOST_OUT%\dual_host_fake_harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_DUAL_HOST_OUT%\dual_host_fake_harness.exe" || exit /b 1
