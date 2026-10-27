@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

if /i "%~1"=="Debug" (
  set "DLW_EDITOR_ANIM_OPT=/Od"
) else if /i "%~1"=="Release" (
  set "DLW_EDITOR_ANIM_OPT=/O2"
) else (
  echo Usage: %~nx0 Debug^|Release
  exit /b 2
)

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
set "DLW_EDITOR_ANIM_COMPILE_OUT=build\editor-animation-fix-compile-%~1"
if exist "%DLW_EDITOR_ANIM_COMPILE_OUT%" rmdir /s /q "%DLW_EDITOR_ANIM_COMPILE_OUT%" || exit /b 1
mkdir "%DLW_EDITOR_ANIM_COMPILE_OUT%" || exit /b 1

set "DLW_EDITOR_ANIM_C_FLAGS=/nologo /c /TC /MT %DLW_EDITOR_ANIM_OPT% /Oi- /GS /guard:cf /W4 /WX /Zl /Zc:wchar_t- /Zc:threadSafeInit- /D_WIN64 /DWIN32_LEAN_AND_MEAN /D_HAS_EXCEPTIONS=0 /D_ITERATOR_DEBUG_LEVEL=0 /Ibuild\generated"
set "DLW_EDITOR_ANIM_CPP_FLAGS=/nologo /c /TP /MT %DLW_EDITOR_ANIM_OPT% /Oi- /GS /guard:cf /W4 /WX /Zl /Zc:wchar_t- /Zc:threadSafeInit- /std:c++17 /GR- /D_WIN64 /DWIN32_LEAN_AND_MEAN /D_HAS_EXCEPTIONS=0 /D_ITERATOR_DEBUG_LEVEL=0 /Ibuild\generated /U_DEBUG"

cl.exe %DLW_EDITOR_ANIM_C_FLAGS% ^
  /Fo"%DLW_EDITOR_ANIM_COMPILE_OUT%\guard.obj" ^
  src\core\dlw_editor_model_animation_guard.c || exit /b 1
cl.exe %DLW_EDITOR_ANIM_C_FLAGS% ^
  /Fo"%DLW_EDITOR_ANIM_COMPILE_OUT%\fix.obj" ^
  src\dlw_editor_model_animation_fix.c || exit /b 1
cl.exe %DLW_EDITOR_ANIM_CPP_FLAGS% ^
  /Fo"%DLW_EDITOR_ANIM_COMPILE_OUT%\exports.obj" ^
  src\dlw_exports.c || exit /b 1

echo Editor animation fix scoped %~1 compile passed.
