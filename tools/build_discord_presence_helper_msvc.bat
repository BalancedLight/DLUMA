@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

set "DLUMA_DISCORD_CONFIG=%~1"
if not defined DLUMA_DISCORD_CONFIG set "DLUMA_DISCORD_CONFIG=Release"
set "DLUMA_DISCORD_OUTPUT=%~2"
if not defined DLUMA_DISCORD_OUTPUT set "DLUMA_DISCORD_OUTPUT=bin\x64\%DLUMA_DISCORD_CONFIG%"

if /I not "%DLUMA_DISCORD_CONFIG%"=="Release" if /I not "%DLUMA_DISCORD_CONFIG%"=="Debug" (
  echo ERROR: configuration must be Release or Debug.
  exit /b 1
)

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)

set "DLUMA_DISCORD_OBJ_DIR=build\obj\discord-presence\%DLUMA_DISCORD_CONFIG%"
if exist "%DLUMA_DISCORD_OBJ_DIR%" rmdir /s /q "%DLUMA_DISCORD_OBJ_DIR%" || exit /b 1
mkdir "%DLUMA_DISCORD_OBJ_DIR%" || exit /b 1
if not exist "%DLUMA_DISCORD_OUTPUT%" mkdir "%DLUMA_DISCORD_OUTPUT%" || exit /b 1

set "DLUMA_DISCORD_RUNTIME_FLAG=/MT"
set "DLUMA_DISCORD_OPT_FLAG=/O2"
if /I "%DLUMA_DISCORD_CONFIG%"=="Debug" (
  set "DLUMA_DISCORD_RUNTIME_FLAG=/MTd"
  set "DLUMA_DISCORD_OPT_FLAG=/Od"
)

cl.exe /nologo /c /TP /std:c++20 /EHsc %DLUMA_DISCORD_RUNTIME_FLAG% ^
  %DLUMA_DISCORD_OPT_FLAG% /W4 /WX /GS /guard:cf ^
  /DDLUMA_DISCORD_PRESENCE_HELPER_EXPORTS ^
  /I"src" ^
  /Fo"%DLUMA_DISCORD_OBJ_DIR%\dlw_discord_presence_helper.obj" ^
  /Fd"%DLUMA_DISCORD_OBJ_DIR%\dlw_discord_presence_helper.pdb" ^
  src\dlw_discord_presence_helper.cpp || exit /b 1

for %%F in ("%DLUMA_DISCORD_OBJ_DIR%\*.obj") do (
  if /I not "%%~nxF"=="dlw_discord_presence_helper.obj" (
    echo ERROR: unexpected Discord helper object: %%~fF
    exit /b 1
  )
)

link.exe /nologo /DLL /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /GUARD:CF ^
  /OUT:"%DLUMA_DISCORD_OUTPUT%\dluma_discord_presence.dll" ^
  /PDB:"%DLUMA_DISCORD_OUTPUT%\dluma_discord_presence.pdb" ^
  "%DLUMA_DISCORD_OBJ_DIR%\dlw_discord_presence_helper.obj" || exit /b 1

python tools\verify_pe.py "%DLUMA_DISCORD_OUTPUT%\dluma_discord_presence.dll" --profile discord-helper || exit /b 1

echo Built Discord desktop RPC helper with no vendor SDK dependency.
exit /b 0
