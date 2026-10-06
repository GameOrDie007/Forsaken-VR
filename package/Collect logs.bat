@echo off
rem Forsaken VR: Collect logs.  Puts the game's logs, its crash reports and
rem settings, Setup's own log and a short description of this PC (Windows,
rem graphics card, VR runtime) in one zip on your desktop, ready to send us.
rem Nothing is sent anywhere by itself.
rem
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
set "SCRIPT=%~dp0tools\setup.ps1"
if not exist "%SCRIPT%" set "SCRIPT=%~dp0files\setup.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Collect %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
