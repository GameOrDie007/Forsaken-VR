@echo off
rem Forsaken VR: Setup.  Finds your copy of Forsaken Remastered (Steam, GOG or any
rem folder) if you have one, makes a VR folder for Forsaken VR, fetches the free
rem ForsakenX game data, and offers a desktop shortcut and your Steam library.
rem Safe to run again: it never touches your pilot, saved games or settings.
rem Drop a folder on this file if it cannot find one by itself.
rem
rem Tools and hubs: Setup.bat -Quiet [-GamePath "<folder>"] [-Json]
rem (README.md, "For tools and hubs").  Exit code 0 done, 1 nothing installed, 2 error.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup.ps1" %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
