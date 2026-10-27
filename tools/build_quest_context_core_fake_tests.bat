@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\quest-context-core-fake mkdir build\quest-context-core-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /I src ^
  /Fd"build\quest-context-core-fake\quest_context_core_fake_harness.pdb" ^
  /Fe"build\quest-context-core-fake\quest_context_core_fake_harness.exe" ^
  tests\quest_context_core_fake_harness.c ^
  src\core\dlw_quest_context_core.c || exit /b 1

build\quest-context-core-fake\quest_context_core_fake_harness.exe || exit /b 1
echo Built and passed: build\quest-context-core-fake\quest_context_core_fake_harness.exe
exit /b 0
