@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
call tools\setup_msvc.bat || exit /b 1
if not exist build\core mkdir build\core || exit /b 1
cl.exe /nologo /std:c++17 /EHsc /MT /W4 /WX /I src /Fobuild\core\ /Febuild\core\controller_tests.exe tests\controller_tests.cpp src\controller.cpp src\camera_motion.cpp || exit /b 1
build\core\controller_tests.exe
exit /b %errorlevel%
