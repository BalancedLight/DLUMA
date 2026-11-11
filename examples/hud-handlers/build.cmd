@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\.."
set "OUT=%CD%\build\examples\hud-handlers"
where cl.exe >nul 2>nul || (echo Run this script from an x64 Visual Studio Developer Command Prompt. & exit /b 1)
where dotnet.exe >nul 2>nul || (echo Install the .NET 10 SDK. & exit /b 1)
where python.exe >nul 2>nul || (echo Install Python. & exit /b 1)
set "C_DIR=%OUT%\mods\native\global\example.hud.c"
set "CPP_DIR=%OUT%\mods\native\global\example.hud.cpp"
set "CSHARP_DIR=%OUT%\mods\csharp\global\example.hud.csharp"
set "LUA_DIR=%OUT%\data\scripts\lua\global\example.hud.lua"
for %%D in ("%C_DIR%" "%CPP_DIR%" "%CSHARP_DIR%" "%LUA_DIR%") do if not exist "%%~D" mkdir "%%~D" || exit /b 1
copy /Y "examples\hud-handlers\c\mod.dluma.json" "%C_DIR%\mod.dluma.json" >nul || exit /b 1
copy /Y "examples\hud-handlers\cpp\mod.dluma.json" "%CPP_DIR%\mod.dluma.json" >nul || exit /b 1
copy /Y "examples\hud-handlers\csharp\mod.dluma.json" "%CSHARP_DIR%\mod.dluma.json" >nul || exit /b 1
copy /Y "examples\hud-handlers\lua\mod.dluma.json" "%LUA_DIR%\mod.dluma.json" >nul || exit /b 1
cl.exe /nologo /LD /TC /W4 /Fo"%OUT%\c.obj" "examples\hud-handlers\c\mod.c" /link /out:"%C_DIR%\hud.mod" /implib:"%OUT%\c.lib" || exit /b 1
cl.exe /nologo /LD /TP /std:c++17 /W4 /Fo"%OUT%\cpp.obj" "examples\hud-handlers\cpp\mod.cpp" /link /out:"%CPP_DIR%\hud.mod" /implib:"%OUT%\cpp.lib" || exit /b 1
dotnet build "examples\hud-handlers\csharp\HudHandlers.csproj" -c Release -o "%OUT%\managed-build" || exit /b 1
copy /Y "%OUT%\managed-build\HudHandlers.dll" "%CSHARP_DIR%\HudHandlers.dll" >nul || exit /b 1
copy /Y "examples\hud-handlers\lua\main.lua" "%LUA_DIR%\main.lua" >nul || exit /b 1
for %%D in ("%C_DIR%" "%CPP_DIR%" "%CSHARP_DIR%" "%LUA_DIR%") do python tools\validate_mod_package.py lock "%%~D" || exit /b 1
for %%D in ("%C_DIR%" "%CPP_DIR%" "%CSHARP_DIR%" "%LUA_DIR%") do python tools\validate_mod_package.py validate "%%~D" --require-lock || exit /b 1
echo Packages staged in %OUT%
