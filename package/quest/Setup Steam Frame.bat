@echo off
rem Forsaken VR on a Steam Frame (standalone, under Lepton) - Setup.
rem The same app as on Quest, installed over Wi-Fi: the Frame in Developer Mode
rem with "Lepton Development" running.  Asks for the Frame's name or address
rem once (Settings, System, Hostname) and keeps it in files\frame-address.txt.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
set "ADDR="
if exist "%~dp0files\frame-address.txt" set /p ADDR=<"%~dp0files\frame-address.txt"
if not "%ADDR%"=="" goto have
echo.
echo   Your Steam Frame's name or address (on the Frame: Settings, System, Hostname).
set /p ADDR=  Frame:
if "%ADDR%"=="" goto none
> "%~dp0files\frame-address.txt" echo %ADDR%
:have
echo   Steam Frame: %ADDR%  (delete files\frame-address.txt to change it)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Serial "%ADDR%" %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
:none
echo   No Frame given; nothing was done.
pause
exit /b 3
