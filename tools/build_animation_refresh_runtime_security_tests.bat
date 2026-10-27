@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
set "DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT=build\animation-refresh-runtime-security"
if exist "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%" rmdir /s /q "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%" || exit /b 1
mkdir "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%" || exit /b 1

for %%F in (dlw_animation_refresh dlw_json_reader) do (
  cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
    /Fo"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\%%F.obj" ^
    "src\core\%%F.c" || exit /b 1
)
for %%F in (dlw_sha256 dlw_project_owner_identity) do (
  cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
    /Fo"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\%%F.obj" ^
    "src\%%F.c" || exit /b 1
)
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\support.obj" ^
  tests\animation_refresh_runtime_test_support.c || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\harness.obj" ^
  tests\animation_refresh_runtime_security_native.c || exit /b 1

link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\animation_refresh_runtime_security.exe" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\dlw_animation_refresh.obj" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\dlw_json_reader.obj" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\dlw_sha256.obj" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\dlw_project_owner_identity.obj" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\harness.obj" ^
  "%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\support.obj" ^
  kernel32.lib || exit /b 1

"%DLW_ANIMATION_REFRESH_RUNTIME_SECURITY_OUT%\animation_refresh_runtime_security.exe" || exit /b 1
