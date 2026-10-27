@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\project-host-exit-fake mkdir build\project-host-exit-fake
python -B tools\prepare_project_host_exit_fake.py || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_WIN64 /Ibuild\generated /Isrc /Ibuild\project-host-exit-fake ^
  /Fo"build\project-host-exit-fake/" /Fe"build\project-host-exit-fake\native_exit.exe" ^
  tests\project_host_exit_fake_harness.c src\core\dlw_mod_owner.c ^
  /link /OPT:REF /INCREMENTAL:NO kernel32.lib user32.lib || exit /b 1
build\project-host-exit-fake\native_exit.exe || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_WIN64 /DDLW_TEST_MANAGED_HOST /Ibuild\generated /Isrc /Ibuild\project-host-exit-fake ^
  /Fo"build\project-host-exit-fake/" /Fe"build\project-host-exit-fake\managed_exit.exe" ^
  tests\project_host_exit_fake_harness.c src\core\dlw_mod_owner.c ^
  /link /OPT:REF /INCREMENTAL:NO kernel32.lib user32.lib || exit /b 1
build\project-host-exit-fake\managed_exit.exe || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_WIN64 /Ibuild\generated /Isrc /Ibuild\project-host-exit-fake ^
  /Fo"build\project-host-exit-fake/" /Fe"build\project-host-exit-fake\lua_exit.exe" ^
  tests\lua_project_exit_fake_harness.c src\core\dlw_mod_owner.c ^
  /link /OPT:REF /INCREMENTAL:NO kernel32.lib user32.lib || exit /b 1
build\project-host-exit-fake\lua_exit.exe || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_WIN64 /Ibuild\generated /Isrc /Ibuild\project-host-exit-fake ^
  /Fo"build\project-host-exit-fake/" /Fe"build\project-host-exit-fake\map_exit_context.exe" ^
  tests\map_exit_context_fake_harness.c src\core\dlw_mod_owner.c ^
  /link /OPT:REF /INCREMENTAL:NO kernel32.lib user32.lib || exit /b 1
build\project-host-exit-fake\map_exit_context.exe || exit /b 1
exit /b 0
