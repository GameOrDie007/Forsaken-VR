@echo off
rem Uninstall from the Steam Frame: copies your pilots, saved games and settings onto this PC,
rem then takes the game out of the Frame's library.  The game's own folder there (saves and
rem settings) stays, so a new install picks them up.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Uninstall -SteamFrame %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
