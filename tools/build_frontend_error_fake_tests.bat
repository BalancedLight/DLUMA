@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\frontend-error-fake mkdir build\frontend-error-fake
cl.exe /nologo /TC /MT /O2 /W4 /WX ^
  /Fo"build\frontend-error-fake/" ^
  /Fe"build\frontend-error-fake\frontend_error_fake_harness.exe" ^
  tests\frontend_error_fake_harness.c src\dlw_frontend_error_core.c || exit /b 1
build\frontend-error-fake\frontend_error_fake_harness.exe || exit /b 1
cl.exe /nologo /c /TC /W4 /WX /D_WIN64 /Oi- /Zl ^
  /Ibuild\generated /Fo"build\frontend-error-fake/" ^
  src\dlw_frontend_error.c src\dlw_shutdown.c src\dlw_lua_runtime.c ^
  src\dlw_native_mods.c src\hosts\dlw_managed_host.c src\game\dlw_game_host_context.c || exit /b 1
exit /b 0
