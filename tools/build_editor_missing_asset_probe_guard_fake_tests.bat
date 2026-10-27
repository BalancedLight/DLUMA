@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1
set "DLW_EDITOR_PROBE_GUARD_OUT=build\editor-missing-asset-probe-guard-fake"
if exist "%DLW_EDITOR_PROBE_GUARD_OUT%" rmdir /s /q "%DLW_EDITOR_PROBE_GUARD_OUT%" || exit /b 1
mkdir "%DLW_EDITOR_PROBE_GUARD_OUT%" || exit /b 1

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_EDITOR_PROBE_GUARD_OUT%\guard.obj" ^
  src\core\dlw_editor_missing_asset_probe_guard.c || exit /b 1
cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_EDITOR_PROBE_GUARD_OUT%\harness.obj" ^
  tests\editor_missing_asset_probe_guard_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /dynamicbase /nxcompat /highentropyva /guard:cf ^
  /out:"%DLW_EDITOR_PROBE_GUARD_OUT%\editor_missing_asset_probe_guard_fake_harness.exe" ^
  "%DLW_EDITOR_PROBE_GUARD_OUT%\guard.obj" ^
  "%DLW_EDITOR_PROBE_GUARD_OUT%\harness.obj" ^
  kernel32.lib || exit /b 1

"%DLW_EDITOR_PROBE_GUARD_OUT%\editor_missing_asset_probe_guard_fake_harness.exe" || exit /b 1
