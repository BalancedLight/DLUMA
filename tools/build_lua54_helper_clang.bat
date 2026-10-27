@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."

where clang-cl.exe >nul 2>nul || (echo ERROR: clang-cl.exe was not found.& exit /b 1)
where lld-link.exe >nul 2>nul || (echo ERROR: lld-link.exe was not found.& exit /b 1)

set "DLUMA_LUA_SRC=third_party\lua-5.4.8\src"
set "DLUMA_LUA_CONFIG=%~1"
if not defined DLUMA_LUA_CONFIG set "DLUMA_LUA_CONFIG=Release"
if /I not "%DLUMA_LUA_CONFIG%"=="Release" if /I not "%DLUMA_LUA_CONFIG%"=="Debug" (
  echo ERROR: configuration must be Release or Debug.
  exit /b 1
)
set "DLUMA_LUA_OBJ=build\obj\lua54-clang\%DLUMA_LUA_CONFIG%"
set "DLUMA_LUA_OUT=bin\x64\Release"
set "DLUMA_LUA_CFLAGS=/MT /O2"
set "DLUMA_LUA_LINKFLAGS=/opt:ref /opt:icf"
if /I "%DLUMA_LUA_CONFIG%"=="Debug" (
  set "DLUMA_LUA_OUT=bin\x64\Debug"
  set "DLUMA_LUA_CFLAGS=/MTd /Od /Zi /Fd"%DLUMA_LUA_OBJ%\vc140.pdb""
  set "DLUMA_LUA_LINKFLAGS=/debug"
)
if exist "%DLUMA_LUA_OBJ%" rmdir /S /Q "%DLUMA_LUA_OBJ%"
mkdir "%DLUMA_LUA_OBJ%" || exit /b 1
if not exist "%DLUMA_LUA_OUT%" mkdir "%DLUMA_LUA_OUT%"

set "DLUMA_LUA_OBJECTS="
set "DLUMA_LUA_EXPECTED="
for %%F in (
  lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject
  lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
  lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib
) do (
  echo [LUA54 CLANG] %%F.c
  clang-cl.exe /nologo /c /TC %DLUMA_LUA_CFLAGS% /GS /W4 /D_CRT_SECURE_NO_WARNINGS ^
    /I"%DLUMA_LUA_SRC%" /Fo"%DLUMA_LUA_OBJ%\%%F.obj" "%DLUMA_LUA_SRC%\%%F.c" || exit /b 1
  set "DLUMA_LUA_OBJECTS=!DLUMA_LUA_OBJECTS! "%DLUMA_LUA_OBJ%\%%F.obj""
  if defined DLUMA_LUA_EXPECTED (
    set "DLUMA_LUA_EXPECTED=!DLUMA_LUA_EXPECTED!,%%F.obj"
  ) else (
    set "DLUMA_LUA_EXPECTED=%%F.obj"
  )
)

echo [LUA54 CLANG] lapi.c (private lua_setglobal target)
clang-cl.exe /nologo /c /TC %DLUMA_LUA_CFLAGS% /GS /W4 /D_CRT_SECURE_NO_WARNINGS ^
  /Dlua_setglobal=dluma_real_lua_setglobal /I"%DLUMA_LUA_SRC%" ^
  /Fo"%DLUMA_LUA_OBJ%\lapi.obj" "%DLUMA_LUA_SRC%\lapi.c" || exit /b 1
set "DLUMA_LUA_OBJECTS=!DLUMA_LUA_OBJECTS! "%DLUMA_LUA_OBJ%\lapi.obj""
set "DLUMA_LUA_EXPECTED=!DLUMA_LUA_EXPECTED!,lapi.obj"

echo [LUA54 CLANG] src\dlw_lua54_helper.c
clang-cl.exe /nologo /c /TC %DLUMA_LUA_CFLAGS% /GS /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /I"%DLUMA_LUA_SRC%" /I"src" /Fo"%DLUMA_LUA_OBJ%\dlw_lua54_helper.obj" ^
  "src\dlw_lua54_helper.c" || exit /b 1
set "DLUMA_LUA_OBJECTS=!DLUMA_LUA_OBJECTS! "%DLUMA_LUA_OBJ%\dlw_lua54_helper.obj""
set "DLUMA_LUA_EXPECTED=!DLUMA_LUA_EXPECTED!,dlw_lua54_helper.obj"

echo [LUA54 CLANG] src\dlw_lua54_player_bridge.c
clang-cl.exe /nologo /c /TC %DLUMA_LUA_CFLAGS% /GS /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /I"%DLUMA_LUA_SRC%" /I"src" /Fo"%DLUMA_LUA_OBJ%\dlw_lua54_player_bridge.obj" ^
  "src\dlw_lua54_player_bridge.c" || exit /b 1
set "DLUMA_LUA_OBJECTS=!DLUMA_LUA_OBJECTS! "%DLUMA_LUA_OBJ%\dlw_lua54_player_bridge.obj""
set "DLUMA_LUA_EXPECTED=!DLUMA_LUA_EXPECTED!,dlw_lua54_player_bridge.obj"

echo [LUA54 CLANG] src\dlw_lua54_stdlib.c
clang-cl.exe /nologo /c /TC %DLUMA_LUA_CFLAGS% /GS /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /I"src" /Fo"%DLUMA_LUA_OBJ%\dlw_lua54_stdlib.obj" ^
  "src\dlw_lua54_stdlib.c" || exit /b 1
set "DLUMA_LUA_OBJECTS=!DLUMA_LUA_OBJECTS! "%DLUMA_LUA_OBJ%\dlw_lua54_stdlib.obj""
set "DLUMA_LUA_EXPECTED=!DLUMA_LUA_EXPECTED!,dlw_lua54_stdlib.obj"

python tools\verify_object_set.py --object-dir "%DLUMA_LUA_OBJ%" --expected "!DLUMA_LUA_EXPECTED!" || exit /b 1
lld-link.exe /nologo /dll /machine:x64 %DLUMA_LUA_LINKFLAGS% ^
  /implib:"%DLUMA_LUA_OBJ%\dluma_lua54.lib" ^
  /out:"%DLUMA_LUA_OUT%\dluma_lua54.dll" !DLUMA_LUA_OBJECTS! || exit /b 1
copy /Y "third_party\lua-5.4.8\LICENSE" "%DLUMA_LUA_OUT%\LICENSE_LUA_5.4.8.txt" >nul || exit /b 1

echo Built: %DLUMA_LUA_OUT%\dluma_lua54.dll
echo Staged: %DLUMA_LUA_OUT%\LICENSE_LUA_5.4.8.txt
exit /b 0
