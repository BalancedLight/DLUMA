@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\workshop-preview-color-fake mkdir build\workshop-preview-color-fake

cl.exe /nologo /TC /MT /O2 /Oi- /W4 /WX /Zc:wchar_t- ^
  /Fo"build\workshop-preview-color-fake\workshop_preview_color_fake_harness.obj" ^
  /Fe"build\workshop-preview-color-fake\workshop_preview_color_fake_harness.exe" ^
  tests\workshop_preview_color_fake_harness.c || exit /b 1

echo Built: build\workshop-preview-color-fake\workshop_preview_color_fake_harness.exe
exit /b 0
