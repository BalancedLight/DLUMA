@echo off
rem Initialize an x64 MSVC environment without assuming a Visual Studio
rem version, edition, or installation directory.

where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0

if defined VS_INSTALL if exist "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" (
  call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 || exit /b 1
  goto :verify
)

set "DLUMA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DLUMA_VSWHERE%" (
  echo ERROR: MSVC is not active and vswhere.exe was not found.
  exit /b 1
)

set "DLUMA_VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%DLUMA_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if not defined DLUMA_VS_INSTALL set "DLUMA_VS_INSTALL=%%I"
if not defined DLUMA_VS_INSTALL (
  echo ERROR: Visual Studio C++ x64 tools were not found.
  exit /b 1
)

call "%DLUMA_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 || exit /b 1

:verify
where cl.exe >nul 2>nul || (
  echo ERROR: cl.exe is unavailable after Visual Studio initialization.
  exit /b 1
)
exit /b 0
