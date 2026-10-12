@echo off
rem Forsaken VR for Quest - Setup.  Downloads the ForsakenX data set (or uses a
rem copy you have), takes the soundtrack and intro from your Forsaken Remastered
rem if you own it, installs the app on your Quest over USB and copies the game
rem to it.  Safe to run again: it copies only what changed and never touches
rem your pilots, saved games or settings.
rem
rem Tools and hubs: Setup.bat -Quiet [-GamePath "<Forsaken Remastered folder>"] [-Json]
rem (README.txt, "For tools and hubs").  Exit code 0 done, 1 no game data,
rem 2 error, 3 no headset.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup-quest.ps1" %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
