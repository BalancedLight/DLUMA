@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\workshop-vfs-refresh-fake mkdir build\workshop-vfs-refresh-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /Zc:wchar_t- ^
  /Fo"build\workshop-vfs-refresh-fake\workshop_vfs_refresh_fake_harness.obj" ^
  /Fd"build\workshop-vfs-refresh-fake\workshop_vfs_refresh_fake_harness.pdb" ^
  /Fe"build\workshop-vfs-refresh-fake\workshop_vfs_refresh_fake_harness.exe" ^
  tests\workshop_vfs_refresh_fake_harness.c || exit /b 1

echo Built: build\workshop-vfs-refresh-fake\workshop_vfs_refresh_fake_harness.exe
exit /b 0
