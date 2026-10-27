@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\mouse-window-lock-fake mkdir build\mouse-window-lock-fake

cl.exe /nologo /TC /MT /O2 /W4 /WX ^
  /Fo"build\mouse-window-lock-fake/" ^
  /Fd"build\mouse-window-lock-fake\mouse_window_lock_fake_harness.pdb" ^
  /Fe"build\mouse-window-lock-fake\mouse_window_lock_fake_harness.exe" ^
  tests\mouse_window_lock_fake_harness.c ^
  src\dlw_mouse_lock_core.c ^
  /link /stack:8388608 kernel32.lib user32.lib || exit /b 1

echo Built: build\mouse-window-lock-fake\mouse_window_lock_fake_harness.exe
exit /b 0
