# Builds the Quest and Steam Frame release folder and zip from the signed app
# (build-quest.ps1 -Release) and package\quest\:
#
#   <Name>\Setup.bat, Setup Steam Frame.bat, Start on Steam Frame.bat,
#          Uninstall.bat, Collect logs.bat, Collect Steam Frame logs.bat,
#          README.txt, THIRD-PARTY.txt, LICENSE, licenses\
#   <Name>\files\setup-quest.ps1, files\setup-common.ps1 (the PC Setup: its search and
#          data functions), files\ForsakenVR.apk, files\vrsplash.png,
#          files\game\ (the scripts, the default settings, the playlist)
#
# Nothing of the game's own data: Setup downloads the ForsakenX data set and
# takes the music and film from the player's Forsaken Remastered.  -Apk takes
# another app (the desk tests use the debug build, which also runs on the emulator).
# Usage: powershell -ExecutionPolicy Bypass -File tools\make-release-quest.ps1 [-Name Forsaken-VR-1.1] [-Apk <file>]
param([string] $Name = 'Forsaken-VR-1.1', [string] $Apk)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).ProviderPath
$Out = Join-Path $Repo 'dist\quest'
$Dir = Join-Path $Out $Name
$Zip = Join-Path $Repo ('dist\' + $Name + '-Quest.zip')
if (-not $Apk) { $Apk = Join-Path $Repo 'platform\quest\app\build\outputs\apk\release\app-release.apk' }
if (-not (Test-Path -LiteralPath $Apk)) { throw "no app: $Apk (build it with tools\build-quest.ps1 -Release)" }

if (Test-Path -LiteralPath $Dir) { Remove-Item -LiteralPath $Dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $Dir 'files\game\configs'), (Join-Path $Dir 'files\game\Music'), (Join-Path $Dir 'licenses') | Out-Null

Copy-Item -LiteralPath $Apk (Join-Path $Dir 'files\ForsakenVR.apk')
Copy-Item (Join-Path $Repo 'package\quest\setup-quest.ps1') (Join-Path $Dir 'files\setup-quest.ps1')
Copy-Item (Join-Path $Repo 'package\setup.ps1') (Join-Path $Dir 'files\setup-common.ps1')	# not setup.ps1: that name marks a PC zip for the installer
Copy-Item (Join-Path $Repo 'art\vrsplash.png') (Join-Path $Dir 'files\vrsplash.png')
# The game's own files that are ours: its scripts, the default settings, our playlist.
robocopy (Join-Path $Repo 'scripts') (Join-Path $Dir 'files\game\scripts') /e /njh /njs /ndl /nc /ns /np | Out-Null
Copy-Item (Join-Path $Repo 'package\debug.txt') (Join-Path $Dir 'files\game\configs\debug.txt')
Copy-Item (Join-Path $Repo 'music\playlist.txt') (Join-Path $Dir 'files\game\Music\playlist.txt')

$texts = @('Setup.bat', 'Setup Steam Frame.bat', 'Start on Steam Frame.bat', 'Uninstall.bat', 'Collect logs.bat', 'Collect Steam Frame logs.bat', 'README.txt', 'THIRD-PARTY.txt')
foreach ($f in $texts) {
    # CRLF for the batch files and the text: Notepad and cmd both want it.
    $t = (Get-Content -Raw -LiteralPath (Join-Path $Repo ('package\quest\' + $f))) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $Dir $f), $t)
}
Copy-Item (Join-Path $Repo 'LICENSE') $Dir

# The licences of what the app carries (THIRD-PARTY.txt lists them).
$pcLic = Join-Path $Repo 'package\licenses'
foreach ($k in @('SDL.txt', 'OpenAL-Soft.txt', 'OpenXR-SDK-loader.txt', 'Lua.txt', 'LuaSocket.txt', 'ENet.txt', 'libpng.txt', 'libogg.txt', 'libvorbis.txt', 'libtheora.txt')) {
    Copy-Item (Join-Path $pcLic $k) (Join-Path $Dir ('licenses\' + $k))
}
Copy-Item (Join-Path $Repo 'package\quest\licenses\LLVM-libc++.txt') (Join-Path $Dir 'licenses\LLVM-libc++.txt')

# Nothing that is not ours to ship: no game data, no player files.
$bad = Get-ChildItem -Recurse -File -LiteralPath $Dir | Where-Object { $_.Extension -in '.mx', '.mxa', '.bsp', '.ogg', '.ogv', '.wav', '.cob', '.kpf', '.sav' -or $_.Name -ieq 'player.txt' -or $_.DirectoryName -match '\\(pilots|savegame|data)(\\|$)' }
if ($bad) { throw ('game data or player files in the release: ' + (($bad | ForEach-Object { $_.Name }) -join ', ')) }
# Public text: no em dashes, and no spaced hyphen used as a dash (the house rule).
foreach ($f in @('README.txt', 'THIRD-PARTY.txt')) {
    $t = Get-Content -Raw -LiteralPath (Join-Path $Dir $f)
    if ($t -match [char]0x2014 -or $t -match ' -- ' -or $t -match '\w - \w') { throw "$f has a dash the house style does not allow" }
}

if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip -Force }
Compress-Archive -Path (Join-Path $Dir '*') -DestinationPath $Zip
Get-ChildItem -Recurse -File -LiteralPath $Dir | Where-Object { $_.FullName -notmatch '\\scripts\\' } | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($Dir.Length + 1) }
''
'zip: {0}  ({1:N1} MB)' -f $Zip, ((Get-Item $Zip).Length / 1MB)
'SHA-256: ' + (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash
