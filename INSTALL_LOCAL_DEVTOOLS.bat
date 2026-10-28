@echo off
setlocal EnableExtensions
if "%~1"=="" (
  echo Usage: %~nx0 "path\to\Dying Light\DevTools\workshop"
  exit /b 2
)
set "WORKSHOP=%~1"
if not exist "%WORKSHOP%" (echo ERROR: workshop root does not exist: %WORKSHOP%& exit /b 1)
set "PACKAGE=%~dp0build\package\Release"
if not exist "%PACKAGE%" (echo ERROR: Release package was not found. Run build_windows_msvc.bat Release first.& exit /b 1)
for /f "delims=" %%V in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\get_loader_version.ps1"') do set "DLUMA_VERSION=%%V"
if not defined DLUMA_VERSION (echo ERROR: Could not determine the numeric loader version.& exit /b 1)
set "DLUMA_MOD_FILENAME=DLUMA (%DLUMA_VERSION%).mod"
mkdir "%WORKSHOP%\DLUMA\mods" 2>nul
copy /y "%PACKAGE%\%DLUMA_MOD_FILENAME%" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\dluma.pak" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\dluma_lua54.dll" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\LICENSE" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\THIRD_PARTY_NOTICES.md" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\LICENSE_LUA_5.4.8.txt" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\dluma_discord_presence.dll" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
copy /y "%PACKAGE%\dluma_nightly_updater.exe" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
xcopy /e /i /y "%PACKAGE%\dluma_runtime" "%WORKSHOP%\DLUMA\mods\dluma_runtime" >nul || exit /b 1
if not exist "%WORKSHOP%\DLUMA\mods\dluma.ini" copy /y "%~dp0config\dluma.ini" "%WORKSHOP%\DLUMA\mods\" >nul || exit /b 1
echo Installed loader to: %WORKSHOP%\DLUMA\mods
exit /b 0
