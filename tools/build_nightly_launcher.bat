@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
set "DLUMA_LAUNCHER_OUT=%~2"
if not defined DLUMA_LAUNCHER_OUT set "DLUMA_LAUNCHER_OUT=build\nightly-launcher"
if not exist "%DLUMA_LAUNCHER_OUT%" mkdir "%DLUMA_LAUNCHER_OUT%" || exit /b 1
cl /nologo /O2 /MT /W4 /WX /GS /guard:cf /TC /Fo"%DLUMA_LAUNCHER_OUT%\nightly_launcher.obj" /Fe"%DLUMA_LAUNCHER_OUT%\dluma_nightly_updater.exe" tools\nightly_launcher.c /link /dynamicbase /nxcompat /highentropyva /guard:cf || exit /b 1
python -c "import pathlib,sys; p=pathlib.Path(sys.argv[1]); assert p.stat().st_size < 256*1024, 'Nightly launcher must stay below 256 KiB'" "%DLUMA_LAUNCHER_OUT%\dluma_nightly_updater.exe" || exit /b 1

python tools\verify_pe.py "%DLUMA_LAUNCHER_OUT%\dluma_nightly_updater.exe" --profile nightly-launcher || exit /b 1
