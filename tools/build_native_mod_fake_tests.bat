@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
set "DLW_FAKE_CONFIG=%~1"
if not defined DLW_FAKE_CONFIG set "DLW_FAKE_CONFIG=Release"
if /I not "%DLW_FAKE_CONFIG%"=="Release" if /I not "%DLW_FAKE_CONFIG%"=="Debug" (
  echo ERROR: configuration must be Release or Debug.
  exit /b 1
)
set "DLW_FAKE_OUT=build\native-mod-fake\%DLW_FAKE_CONFIG%"
if not exist "%DLW_FAKE_OUT%" mkdir "%DLW_FAKE_OUT%"
set "DLW_FAKE_CFLAGS=/nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /Zl /W4 /WX"
if /I "%DLW_FAKE_CONFIG%"=="Debug" set "DLW_FAKE_CFLAGS=/nologo /c /TC /MT /Od /Zi /Oi- /GS /guard:cf /Zl /W4 /WX"

for %%F in (fixture_v2_a fixture_v2_b fixture_v2_fail fixture_legacy_v1 fixture_legacy) do (
  cl.exe %DLW_FAKE_CFLAGS% /Fo"%DLW_FAKE_OUT%\%%F.obj" /Fd"%DLW_FAKE_OUT%\%%F.pdb" "tests\native_mod_fixtures\%%F.c" || exit /b 1
  link.exe /nologo /dll /machine:x64 /entry:DllMain /nodefaultlib /dynamicbase /nxcompat /highentropyva /guard:cf ^
    /out:"%DLW_FAKE_OUT%\%%F.mod" "%DLW_FAKE_OUT%\%%F.obj" ^
    kernel32.lib BufferOverflowU.lib libcmt.lib || exit /b 1
)

cl.exe /nologo /c /TC /MT /O2 /Oi- /GS /guard:cf /W4 /WX ^
  /Fo"%DLW_FAKE_OUT%\native_mod_chain_fake_harness.obj" ^
  tests\native_mod_chain_fake_harness.c || exit /b 1
link.exe /nologo /machine:x64 /out:"%DLW_FAKE_OUT%\native_mod_chain_fake_harness.exe" ^
  "%DLW_FAKE_OUT%\native_mod_chain_fake_harness.obj" kernel32.lib || exit /b 1

echo Built: %DLW_FAKE_OUT%\native_mod_chain_fake_harness.exe
exit /b 0
