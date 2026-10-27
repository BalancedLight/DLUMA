@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || (
  call tools\setup_msvc.bat || exit /b 1
)
if not exist build\loader-pak-exact-join-fake mkdir build\loader-pak-exact-join-fake

cl.exe /nologo /TP /MT /O2 /Oi- /W4 /WX /EHs-c- /GR- /std:c++17 ^
  /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /I src ^
  /Fo"build\loader-pak-exact-join-fake/" ^
  /Fd"build\loader-pak-exact-join-fake\loader_pak_exact_join_fake_harness.pdb" ^
  /Fe"build\loader-pak-exact-join-fake\loader_pak_exact_join_fake_harness.exe" ^
  tests\loader_pak_exact_join_fake_harness.cpp ^
  src\fixes\custom_map_asset_isolation\StartupFilesystemSourceClassifier.cpp ^
  src\dlw_process_lifetime_sources.c ^
  /link kernel32.lib user32.lib || exit /b 1

"build\loader-pak-exact-join-fake\loader_pak_exact_join_fake_harness.exe" || exit /b 1
echo Built and passed: build\loader-pak-exact-join-fake\loader_pak_exact_join_fake_harness.exe
exit /b 0
