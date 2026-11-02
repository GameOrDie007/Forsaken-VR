@echo off
rem Collect logs from the Steam Frame: the game's logs and settings there, the library entry's
rem log and Setup's log, in one zip on your desktop, ready to send us.  Nothing is sent
rem anywhere by itself.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Collect -SteamFrame %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
