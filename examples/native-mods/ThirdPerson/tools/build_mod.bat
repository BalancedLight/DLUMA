@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
set "TP_TOOLCHAIN=%~1"
set "TP_CONFIG=%~2"
if not defined TP_CONFIG set "TP_CONFIG=Release"
set "TP_DEPLOY=%~3"
if /I not "%TP_CONFIG%"=="Release" if /I not "%TP_CONFIG%"=="Debug" (
    echo ERROR: Configuration must be Release or Debug.
    exit /b 1
)
if defined TP_DEPLOY if /I not "%TP_CONFIG%"=="Release" (
    echo ERROR: Deployment requires a Release package.
    exit /b 1
)
set "TP_CC=cl.exe"
if /I "%TP_TOOLCHAIN%"=="clang" (
    where clang-cl.exe >nul 2>nul || (
        echo ERROR: clang-cl.exe was not found. Add LLVM to PATH.
        exit /b 1
    )
    set "TP_CC=clang-cl.exe"
)
set "TP_OUT=build\%TP_TOOLCHAIN%\%TP_CONFIG%"
if not exist "%TP_OUT%" mkdir "%TP_OUT%" || exit /b 1
set "TP_FLAGS=/nologo /std:c++17 /EHsc /MT /W4 /WX /GS /guard:cf /I sdk /I src /I third_party\minhook\include /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
set "TP_CFLAGS=/nologo /MT /W3 /WX- /GS /guard:cf /I third_party\minhook\include /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
if /I "%TP_CONFIG%"=="Release" (
    set "TP_FLAGS=%TP_FLAGS% /O2 /DNDEBUG"
    set "TP_CFLAGS=%TP_CFLAGS% /O2 /DNDEBUG"
) else (
    set "TP_FLAGS=%TP_FLAGS% /Od /Zi"
    set "TP_CFLAGS=%TP_CFLAGS% /Od /Zi"
)
rem Build the pinned upstream C dependency with its own warning policy.
%TP_CC% %TP_CFLAGS% /c /Fo"%TP_OUT%\\" third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c || exit /b 1
%TP_CC% %TP_FLAGS% /LD /Fo"%TP_OUT%\\" /Fe"%TP_OUT%\ThirdPerson.dll" src\mod.cpp src\controller.cpp src\camera_motion.cpp src\engine_adapter.cpp src\engine_reticle.cpp src\engine_actions.cpp src\engine_interactions.cpp src\interaction_selection.cpp "%TP_OUT%\buffer.obj" "%TP_OUT%\hook.obj" "%TP_OUT%\trampoline.obj" "%TP_OUT%\hde64.obj" /link /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf /PDBALTPATH:ThirdPerson.pdb kernel32.lib user32.lib bcrypt.lib || exit /b 1
echo Built: %TP_OUT%\ThirdPerson.dll
if /I "%TP_CONFIG%"=="Release" (
    python tools\stage_package.py --module "%TP_OUT%\ThirdPerson.dll" || exit /b 1
    if defined TP_DEPLOY (
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\deploy_local.ps1 -Source "%CD%\dist\mods\native\global\thirdperson.camera" -Destination "%TP_DEPLOY%" || exit /b 1
    )
)
exit /b 0
