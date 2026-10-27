@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "CLANG=clang-cl.exe"
set "LINKER=lld-link.exe"
set "DLUMA_CLANG_CONFIG=%~1"
if not defined DLUMA_CLANG_CONFIG set "DLUMA_CLANG_CONFIG=Release"
if /I not "%DLUMA_CLANG_CONFIG%"=="Release" if /I not "%DLUMA_CLANG_CONFIG%"=="Debug" (
  echo ERROR: configuration must be Release or Debug.
  exit /b 1
)
for /f "delims=" %%V in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\get_loader_version.ps1') do set "DLUMA_VERSION=%%V"
if not defined DLUMA_VERSION (echo ERROR: Could not determine the numeric loader version.& exit /b 1)
set "DLUMA_MOD_FILENAME=DLUMA (%DLUMA_VERSION%).mod"
where %CLANG% >nul 2>nul || (echo ERROR: clang-cl.exe was not found. Install LLVM for Windows.& exit /b 1)
where %LINKER% >nul 2>nul || (echo ERROR: lld-link.exe was not found. Install LLVM for Windows.& exit /b 1)
where python.exe >nul 2>nul || (echo ERROR: python.exe is required to generate build provenance.& exit /b 1)
if not defined LIB (echo ERROR: Run this from a Visual Studio x64 Native Tools command prompt so the Windows SDK security libraries are available.& exit /b 1)

set "DLUMA_CLANG_OBJ_DIR=build\obj\clang-batch\%DLUMA_CLANG_CONFIG%"
set "DLUMA_CLANG_OUTPUT_DIR=bin\x64\%DLUMA_CLANG_CONFIG%"
set "DLUMA_CLANG_CFLAGS=/MT /O2 /U_DEBUG"
set "DLUMA_CLANG_LINKFLAGS=/opt:ref /opt:icf"
set "DLUMA_CLANG_RUNTIME_LIB=libcmt.lib"
if /I "%DLUMA_CLANG_CONFIG%"=="Debug" (
  set "DLUMA_CLANG_CFLAGS=/MTd /Od /Zi /Fd"%DLUMA_CLANG_OBJ_DIR%\vc140.pdb" /U_DEBUG"
  set "DLUMA_CLANG_LINKFLAGS=/debug"
  set "DLUMA_CLANG_RUNTIME_LIB=libcmtd.lib"
)
if exist "%DLUMA_CLANG_OBJ_DIR%" rmdir /S /Q "%DLUMA_CLANG_OBJ_DIR%"
mkdir "%DLUMA_CLANG_OBJ_DIR%" || exit /b 1
if not exist "%DLUMA_CLANG_OUTPUT_DIR%" mkdir "%DLUMA_CLANG_OUTPUT_DIR%"
if not exist build\generated mkdir build\generated
set "DLUMA_PROVENANCE_ARCHIVE_FLAG="
if /I "%DLUMA_ALLOW_SOURCE_ARCHIVE_PROVENANCE%"=="1" set "DLUMA_PROVENANCE_ARCHIVE_FLAG=--allow-source-archive"
python tools\generate_build_provenance.py --repo "%CD%" --output "%CD%\build\generated\dlw_build_provenance.h" %DLUMA_PROVENANCE_ARCHIVE_FLAG% || exit /b 1
rem Source enumeration uses Directory.Build.targets to compile the UI lifecycle wrapper once.
set "DLUMA_CLANG_OBJECTS="
set "DLUMA_CLANG_EXPECTED="
for /f "usebackq delims=" %%F in (`python tools\list_vcxproj_sources.py --project DLUMA.vcxproj --kind c --exclude src\dlw_entry.c --exclude src\dlw_security.c`) do (
  echo [CC] %%F
  %CLANG% /nologo /c /TC %DLUMA_CLANG_CFLAGS% /I"build\generated" /Oi- /GS /guard:cf /Zl /W4 /WX /Fo"%DLUMA_CLANG_OBJ_DIR%\%%~nF.obj" /clang:-fno-builtin "%%F" || exit /b 1
  set "DLUMA_CLANG_OBJECTS=!DLUMA_CLANG_OBJECTS! "%DLUMA_CLANG_OBJ_DIR%\%%~nF.obj""
  if defined DLUMA_CLANG_EXPECTED (
    set "DLUMA_CLANG_EXPECTED=!DLUMA_CLANG_EXPECTED!,%%~nF.obj"
  ) else (
    set "DLUMA_CLANG_EXPECTED=%%~nF.obj"
  )
)
echo [CC] src\dlw_entry.c
%CLANG% /nologo /c /TC %DLUMA_CLANG_CFLAGS% /Oi- /GS- /guard:cf /Zl /W4 /WX /Fo"%DLUMA_CLANG_OBJ_DIR%\dlw_entry.obj" /clang:-fno-builtin "src\dlw_entry.c" || exit /b 1
set "DLUMA_CLANG_OBJECTS=!DLUMA_CLANG_OBJECTS! "%DLUMA_CLANG_OBJ_DIR%\dlw_entry.obj""
set "DLUMA_CLANG_EXPECTED=!DLUMA_CLANG_EXPECTED!,dlw_entry.obj"
echo [CC] src\dlw_security.c
%CLANG% /nologo /c /TC %DLUMA_CLANG_CFLAGS% /Oi- /GS- /guard:cf /Zl /W4 /WX /Fo"%DLUMA_CLANG_OBJ_DIR%\dlw_security.obj" /clang:-fno-builtin "src\dlw_security.c" || exit /b 1
set "DLUMA_CLANG_OBJECTS=!DLUMA_CLANG_OBJECTS! "%DLUMA_CLANG_OBJ_DIR%\dlw_security.obj""
set "DLUMA_CLANG_EXPECTED=!DLUMA_CLANG_EXPECTED!,dlw_security.obj"
for /f "usebackq delims=" %%F in (`python tools\list_vcxproj_sources.py --project DLUMA.vcxproj --kind cpp`) do (
  echo [CXX] %%F
  %CLANG% /nologo /c /TP %DLUMA_CLANG_CFLAGS% /I"build\generated" /std:c++17 /EHs-c- /GR- /Zc:threadSafeInit- /Zc:wchar_t- /D_HAS_EXCEPTIONS=0 /D_ITERATOR_DEBUG_LEVEL=0 /Oi- /GS /guard:cf /Zl /W4 /WX /Fo"%DLUMA_CLANG_OBJ_DIR%\%%~nF.obj" /clang:-fno-builtin "%%F" || exit /b 1
  set "DLUMA_CLANG_OBJECTS=!DLUMA_CLANG_OBJECTS! "%DLUMA_CLANG_OBJ_DIR%\%%~nF.obj""
  set "DLUMA_CLANG_EXPECTED=!DLUMA_CLANG_EXPECTED!,%%~nF.obj"
)
%CLANG% /nologo /c /Fo"%DLUMA_CLANG_OBJ_DIR%\chkstk.obj" src\chkstk.s || exit /b 1
set "DLUMA_CLANG_OBJECTS=!DLUMA_CLANG_OBJECTS! "%DLUMA_CLANG_OBJ_DIR%\chkstk.obj""
set "DLUMA_CLANG_EXPECTED=!DLUMA_CLANG_EXPECTED!,chkstk.obj"
python tools\verify_object_set.py --object-dir "%DLUMA_CLANG_OBJ_DIR%" --expected "!DLUMA_CLANG_EXPECTED!" || exit /b 1
%LINKER% /dll /machine:x64 /entry:DllMain /nodefaultlib %DLUMA_CLANG_LINKFLAGS% /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /def:build\exports.def /out:"%DLUMA_CLANG_OUTPUT_DIR%\%DLUMA_MOD_FILENAME%" ^
  !DLUMA_CLANG_OBJECTS! kernel32.lib user32.lib BufferOverflowU.lib libvcruntime.lib %DLUMA_CLANG_RUNTIME_LIB% || exit /b 1

call tools\build_lua54_helper_clang.bat %DLUMA_CLANG_CONFIG% || exit /b 1

echo Built development binaries: %DLUMA_CLANG_OUTPUT_DIR%\%DLUMA_MOD_FILENAME%
echo Use build_windows_msvc.bat Release Offline for a complete Workshop package.
exit /b 0
