@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\.."
set "OUT=%CD%\build\examples\hud-assets"
set "LUA=%OUT%\data\scripts\lua\project\example.hud.assets.lua"
set "CS=%OUT%\mods\csharp\project\example.hud.assets.csharp"
where dotnet.exe >nul 2>nul || (echo Install the .NET 10 SDK. & exit /b 1)
where python.exe >nul 2>nul || (echo Install Python. & exit /b 1)
if not exist "%LUA%" mkdir "%LUA%" || exit /b 1
if not exist "%CS%" mkdir "%CS%" || exit /b 1
copy /Y examples\hud-assets\lua\main.lua "%LUA%\main.lua" >nul || exit /b 1
copy /Y examples\hud-assets\lua\mod.dluma.json "%LUA%\mod.dluma.json" >nul || exit /b 1
copy /Y examples\hud-assets\csharp\mod.dluma.json "%CS%\mod.dluma.json" >nul || exit /b 1
dotnet build examples\hud-assets\csharp\HudAssets.csproj -c Release -o "%OUT%\managed-build" || exit /b 1
copy /Y "%OUT%\managed-build\HudAssets.dll" "%CS%\HudAssets.dll" >nul || exit /b 1
for %%D in ("%LUA%" "%CS%") do python tools\validate_mod_package.py lock "%%~D" || exit /b 1
for %%D in ("%LUA%" "%CS%") do python tools\validate_mod_package.py validate "%%~D" --require-lock || exit /b 1
echo Packages staged in %OUT%
