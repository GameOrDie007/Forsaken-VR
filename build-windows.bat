@echo off
REM Builds projectx.exe. See BUILD-WINDOWS.md for prerequisites.
REM Assumes MSYS2 is at C:\msys64: edit MSYS below if yours is elsewhere.

set MSYS=C:\msys64

if not exist "%MSYS%\usr\bin\bash.exe" (
	echo.
	echo ERROR: MSYS2 not found at %MSYS%
	echo Install it with:  winget install --id MSYS2.MSYS2 -e
	echo then see BUILD-WINDOWS.md
	echo.
	pause
	exit /b 1
)

REM A running game holds projectx.exe open, and the linker then fails with a
REM bare "Permission denied" that looks like a code error. Catch it here.
tasklist /FI "IMAGENAME eq projectx.exe" 2>NUL | find /I "projectx.exe" >NUL
if not errorlevel 1 (
	echo.
	echo ERROR: the game is still running, so projectx.exe cannot be replaced.
	echo Close the ProjectX window ^(Alt+F4^), or end projectx.exe in Task Manager,
	echo then run this again.
	echo.
	pause
	exit /b 1
)

"%MSYS%\usr\bin\bash.exe" -lc "cd '%~dp0' && export PATH=/mingw64/bin:$PATH && export PKG_CONFIG_PATH=$PWD/libs/lib/pkgconfig:$PKG_CONFIG_PATH && bash build-luasocket.sh && make SDL=2 GL=3 MINGW=1 -j8"

if errorlevel 1 (
	echo.
	echo BUILD FAILED -- scroll up for the first line containing "error:"
	echo.
	pause
	exit /b 1
)

echo.
echo BUILD OK -- run run-windows.bat to play
echo.
pause
