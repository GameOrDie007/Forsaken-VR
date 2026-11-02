@echo off
rem Starts Forsaken VR on the Steam Frame, as its library would (it is there too: Library,
rem then Forsaken VR).  Run "Setup Steam Frame.bat" first.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" -Start -SteamFrame %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
