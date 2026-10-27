@echo off
setlocal EnableExtensions
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from a VS x64 Native Tools prompt.& exit /b 1)
set "DLUMA_EXAMPLE_CONFIG=%~1"
if not defined DLUMA_EXAMPLE_CONFIG set "DLUMA_EXAMPLE_CONFIG=Release"
if /I not "%DLUMA_EXAMPLE_CONFIG%"=="Release" if /I not "%DLUMA_EXAMPLE_CONFIG%"=="Debug" exit /b 1
set "DLUMA_EXAMPLE_OUT=build\msvc\%DLUMA_EXAMPLE_CONFIG%"
set "DLUMA_EXAMPLE_PACKAGE=mods\native\global\example.native.dllmod"
if exist "%DLUMA_EXAMPLE_OUT%" rmdir /s /q "%DLUMA_EXAMPLE_OUT%" || exit /b 1
mkdir "%DLUMA_EXAMPLE_OUT%" || exit /b 1
set "DLUMA_EXAMPLE_CFLAGS=/nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /Zl /W4 /WX"
set "DLUMA_EXAMPLE_LINK=/nologo /dll /machine:x64 /entry:DllMain /nodefaultlib /dynamicbase /nxcompat /highentropyva /guard:cf"
if /I "%DLUMA_EXAMPLE_CONFIG%"=="Debug" set "DLUMA_EXAMPLE_CFLAGS=/nologo /c /TC /MT /Od /Zi /Oi- /GS /guard:cf /Zl /W4 /WX"
cl.exe %DLUMA_EXAMPLE_CFLAGS% /Fo"%DLUMA_EXAMPLE_OUT%\example.obj" /Fd"%DLUMA_EXAMPLE_OUT%\compiler.pdb" dluma_native_mod_example.c || exit /b 1
link.exe %DLUMA_EXAMPLE_LINK% /out:"%DLUMA_EXAMPLE_OUT%\example.mod" "%DLUMA_EXAMPLE_OUT%\example.obj" kernel32.lib BufferOverflowU.lib libcmt.lib || exit /b 1
echo Built: %DLUMA_EXAMPLE_OUT%\example.mod
if /I "%DLUMA_EXAMPLE_CONFIG%"=="Release" (
  if not exist "%DLUMA_EXAMPLE_PACKAGE%" mkdir "%DLUMA_EXAMPLE_PACKAGE%" || exit /b 1
  copy /Y "%DLUMA_EXAMPLE_OUT%\example.mod" "%DLUMA_EXAMPLE_PACKAGE%\example.mod" >nul || exit /b 1
  python ..\..\..\tools\validate_mod_package.py lock "%CD%\%DLUMA_EXAMPLE_PACKAGE%" || exit /b 1
  echo Locked: %DLUMA_EXAMPLE_PACKAGE%
)
