@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_MANIFEST_OUT=build\manifest-fake"
if exist "%DLW_MANIFEST_OUT%" rmdir /s /q "%DLW_MANIFEST_OUT%" || exit /b 1
mkdir "%DLW_MANIFEST_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_MANIFEST_OUT%\dlw_json_reader.obj" ^
  src\core\dlw_json_reader.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_MANIFEST_OUT%\dlw_mod_manifest.obj" ^
  src\core\dlw_mod_manifest.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_MANIFEST_OUT%\dlw_mod_manifest_fake_harness.obj" ^
  tests\dlw_mod_manifest_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_MANIFEST_OUT%\dlw_mod_manifest_fake_harness.exe" ^
  "%DLW_MANIFEST_OUT%\dlw_json_reader.obj" ^
  "%DLW_MANIFEST_OUT%\dlw_mod_manifest.obj" ^
  "%DLW_MANIFEST_OUT%\dlw_mod_manifest_fake_harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_MANIFEST_OUT%\dlw_mod_manifest_fake_harness.exe" || exit /b 1
