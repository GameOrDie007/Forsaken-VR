@echo off
rem Forsaken VR for Quest - Collect logs.  With the headset plugged in, puts
rem the game's logs and settings from the headset, Setup's logs and a line on
rem the headset (model, version) in one zip on your desktop, ready to send us.
rem Nothing is sent anywhere by itself.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Collect %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
