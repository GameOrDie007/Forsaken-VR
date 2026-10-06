@echo off
REM One-time setup. Assembles the game data from copies you already own.
REM Everything it does is explained by setup.ps1 next to this file.
pushd "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
echo.
pause
