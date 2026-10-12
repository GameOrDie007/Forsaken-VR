@echo off
rem Forsaken VR on a Steam Frame: starts the game on the Frame
rem (Lepton has no launcher for it).  Run "Setup Steam Frame.bat" first.
setlocal
set "ADDR="
if exist "%~dp0files\frame-address.txt" set /p ADDR=<"%~dp0files\frame-address.txt"
if "%ADDR%"=="" goto none
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Start -Serial "%ADDR%"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
:none
echo   Run "Setup Steam Frame.bat" first: it asks for the Frame's name.
pause
exit /b 3
