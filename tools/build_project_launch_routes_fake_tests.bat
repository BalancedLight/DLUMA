@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

where cl.exe >nul 2>nul || call tools\setup_msvc.bat || exit /b 1

set "DLW_ROUTE_BUILD=build\project-launch-routes-fake"
if not exist "%DLW_ROUTE_BUILD%" mkdir "%DLW_ROUTE_BUILD%" || exit /b 1
cl.exe /nologo /std:c11 /W4 /WX /EHsc- /DWIN32_LEAN_AND_MEAN ^
  /I src ^
  /Fo"%DLW_ROUTE_BUILD%\\" ^
  /Fd"%DLW_ROUTE_BUILD%\project_launch_routes_fake_harness.pdb" ^
  /Fe"%DLW_ROUTE_BUILD%\project_launch_routes_fake_harness.exe" ^
  tests\project_launch_routes_fake_harness.c ^
  src\dlw_project_launch_routes.c ^
  src\dlw_project_launch_admission.c ^
  /link /incremental:no || exit /b 1
"%DLW_ROUTE_BUILD%\project_launch_routes_fake_harness.exe" || exit /b 1
echo Built and passed: %DLW_ROUTE_BUILD%\project_launch_routes_fake_harness.exe
exit /b 0
