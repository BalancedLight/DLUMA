@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\nightly-offer mkdir build\nightly-offer || exit /b 1
cl /nologo /W4 /WX /MT /TC /Fo"build\nightly-offer\\" /Fe"build\nightly-offer\nightly_offer_core_harness.exe" tests\nightly_offer_core_harness.c src\core\dlw_nightly_offer_core.c src\core\dlw_json_reader.c || exit /b 1
build\nightly-offer\nightly_offer_core_harness.exe || exit /b 1
