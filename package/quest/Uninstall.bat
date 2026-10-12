@echo off
rem Forsaken VR for Quest - Uninstall.  Copies your pilots, saved games and
rem settings from the headset onto this PC (Documents\Forsaken VR\Quest
rem saves), then removes the app from the headset, with the game files Setup
rem gave it.  Setup puts the pilots back if you install again.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Uninstall %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
