@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call tools\build_mod.bat clang "%~1" "%~2"
exit /b %errorlevel%
