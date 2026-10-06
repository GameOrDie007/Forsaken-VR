@echo off
REM Thin launcher for package-windows.ps1, where the real logic lives.
REM
REM   package-windows.bat           full personal copy, game data included.
REM                                 NOT publishable.
REM   package-windows.bat release   our code only, verified clean. Ship this.
REM
setlocal
cd /d "%~dp0"
set MODE=%1
if "%MODE%"=="" set MODE=full
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1" -Mode %MODE%
if errorlevel 1 (
	echo.
	echo Packaging FAILED.
	pause
	exit /b 1
)
pause
endlocal
