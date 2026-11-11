@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
where cl.exe >nul 2>nul || (call tools\setup_msvc.bat || exit /b 1)
if not exist "build\generated\editor-theme\editor_theme_palette.generated.h" (python tools\generate_editor_theme.py || exit /b 1)
set "DLW_GIT_TEST_OUT=build\editor-git-tests"
if not exist "%DLW_GIT_TEST_OUT%" mkdir "%DLW_GIT_TEST_OUT%" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_ui_guard_harness.obj" ^
  tests\editor_ui_guard_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_ui_guard_harness.exe" ^
  /link user32.lib gdi32.lib comctl32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_ui_guard_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_git_retirement_harness.obj" ^
  tests\editor_git_retirement_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_git_retirement_harness.exe" ^
  /link user32.lib bcrypt.lib advapi32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_git_retirement_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_git_lease_harness.obj" ^
  tests\editor_git_lease_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_git_lease_harness.exe" || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_git_lease_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_git_guard_harness.obj" ^
  tests\editor_git_guard_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_git_guard_harness.exe" || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_git_guard_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_git_host_guard_harness.obj" ^
  tests\editor_git_host_guard_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_git_host_guard_harness.exe" ^
  /link /OPT:REF user32.lib bcrypt.lib advapi32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_git_host_guard_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_git_protocol_harness.obj" ^
  tests\editor_git_protocol_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_git_protocol_harness.exe" ^
  /link user32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_git_protocol_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /Gy /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_ui_coverage_harness.obj" ^
  tests\editor_ui_coverage_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_ui_coverage_harness.exe" ^
  /link /OPT:REF user32.lib gdi32.lib comctl32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_ui_coverage_harness.exe" || exit /b 1
cl.exe /nologo /TC /MT /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS ^
  /Fo"%DLW_GIT_TEST_OUT%\editor_options_guard_harness.obj" ^
  tests\editor_options_guard_harness.c /Fe"%DLW_GIT_TEST_OUT%\editor_options_guard_harness.exe" ^
  /link user32.lib || exit /b 1
"%DLW_GIT_TEST_OUT%\editor_options_guard_harness.exe" || exit /b 1
