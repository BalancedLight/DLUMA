@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_PACKAGE_RUNTIME_OUT=build\package-runtime"
if exist "%DLW_PACKAGE_RUNTIME_OUT%" rmdir /s /q "%DLW_PACKAGE_RUNTIME_OUT%" || exit /b 1
mkdir "%DLW_PACKAGE_RUNTIME_OUT%" || exit /b 1
set "DLW_PACKAGE_RUNTIME_CFLAGS=/nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX"

for %%F in (dlw_json_reader dlw_mod_manifest dlw_mod_package_runtime) do (
  cl.exe %DLW_PACKAGE_RUNTIME_CFLAGS% ^
    /Fo"%DLW_PACKAGE_RUNTIME_OUT%\%%F.obj" ^
    "src\core\%%F.c" || exit /b 1
)
for %%F in (dlw_path_security dlw_sha256) do (
  cl.exe %DLW_PACKAGE_RUNTIME_CFLAGS% ^
    /Fo"%DLW_PACKAGE_RUNTIME_OUT%\%%F.obj" ^
    "src\%%F.c" || exit /b 1
)
cl.exe %DLW_PACKAGE_RUNTIME_CFLAGS% ^
  /Fo"%DLW_PACKAGE_RUNTIME_OUT%\test_shim.obj" ^
  tests\dlw_package_runtime_test_shim.c || exit /b 1
cl.exe %DLW_PACKAGE_RUNTIME_CFLAGS% ^
  /Fo"%DLW_PACKAGE_RUNTIME_OUT%\harness.obj" ^
  tests\dlw_mod_package_runtime_harness.c || exit /b 1

link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_PACKAGE_RUNTIME_OUT%\dlw_mod_package_runtime_harness.exe" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\harness.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\dlw_json_reader.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\dlw_mod_manifest.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\dlw_mod_package_runtime.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\test_shim.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\dlw_path_security.obj" ^
  "%DLW_PACKAGE_RUNTIME_OUT%\dlw_sha256.obj" ^
  kernel32.lib || exit /b 1
