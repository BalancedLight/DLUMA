@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Some GUI hosts inject both PATH and Path. MSBuild/.NET rejects that
rem duplicate environment key even though Win32 lookup is case-insensitive.
set "DLUMA_SAVED_SEARCH_PATH=%Path%"
set "PATH="
set "Path=%DLUMA_SAVED_SEARCH_PATH%"

where msbuild.exe >nul 2>nul || (
  echo ERROR: Run this from a Visual Studio x64 Native Tools command prompt.
  exit /b 1
)

set "DLUMA_MSVC_CONFIG=%~1"
if not defined DLUMA_MSVC_CONFIG set "DLUMA_MSVC_CONFIG=Release"
if /I not "%DLUMA_MSVC_CONFIG%"=="Release" if /I not "%DLUMA_MSVC_CONFIG%"=="Debug" (
  echo ERROR: configuration must be Release or Debug.
  exit /b 1
)

set "DLUMA_MSVC_OFFLINE_PROPERTY="
rem Offline preserves local props but suppresses their optional deployment.
if /I "%~2"=="Offline" set "DLUMA_MSVC_OFFLINE_PROPERTY=/p:DlumaAutoDeployLocal=false"
if not "%~2"=="" if /I not "%~2"=="Offline" (
  echo ERROR: optional second argument must be Offline.
  exit /b 1
)

msbuild.exe DLUMA.vcxproj /nologo /m /t:Build ^
  /p:Configuration=%DLUMA_MSVC_CONFIG% /p:Platform=x64 ^
  %DLUMA_MSVC_OFFLINE_PROPERTY% || exit /b 1


echo Built complete %DLUMA_MSVC_CONFIG% DLUMA Workshop package.
exit /b 0
