@echo off
REM Launches projectx.exe with the MinGW runtime DLLs on PATH.
REM Any arguments you pass are forwarded to the game, e.g.
REM     run-windows.bat -fullscreen
REM
REM Debug logging is on by default and lands in logs\<date>.txt.
REM That log is the only diagnostic output: the exe is a GUI-subsystem
REM binary and prints nothing to a console. See BUILD-WINDOWS.md.

set MSYS=C:\msys64

if not exist "%~dp0projectx.exe" (
	echo.
	echo ERROR: projectx.exe not found. Run build-windows.bat first.
	echo.
	pause
	exit /b 1
)

set PATH=%MSYS%\mingw64\bin;%PATH%
pushd "%~dp0"
"%~dp0projectx.exe" -window -debug -log %*
