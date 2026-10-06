<#
    Builds a copyable folder from the current build.

    Two modes, and the difference between them is legal, not just practical:

      full     (default)  Everything needed to play, game data included. For
                          copying to another PC of your own. NOT publishable:
                          Data/ is the ForsakenX data set, and Music/ + Movies/
                          are byte-identical to files from Forsaken Remastered,
                          which is Nightdive's commercial release.

      release             Our own code and nothing else. The player supplies the
                          game data by running setup.bat, which reads it from
                          their own installs. This is the only mode whose output
                          may be uploaded anywhere.

    Release output is checked against an allow-list of file extensions before the
    build is declared finished. Anything unexpected fails the build rather than
    being quietly published: the point is that this does not depend on anyone
    remembering the rule at the end of a long day.

    Usage:   .\package-windows.ps1            # full, personal
             .\package-windows.ps1 release    # distributable
             .\package-windows.ps1 release -Exe <path>
                 # with a given projectx.exe: the exact build that was tested,
                 # since a rebuild after a comment change differs in its bytes
#>
param(
    [ValidateSet('full', 'release')]
    [string]$Mode = 'full',
    [string]$Exe = ''
)

$ErrorActionPreference = 'Stop'

$src     = $PSScriptRoot
$msysBin = 'C:\msys64\mingw64\bin'

if ($Mode -eq 'release') {
    $out = Join-Path $src 'dist\ForsakenVR-release'
} else {
    $out = Join-Path $src 'dist\ForsakenVR'
}

$exe = if ($Exe) { (Resolve-Path -LiteralPath $Exe).ProviderPath } else { Join-Path $src 'projectx.exe' }
if ($Exe) { Write-Host ("  projectx.exe from {0}" -f $exe) }
if (-not (Test-Path $exe)) {
    Write-Host ''
    Write-Host 'ERROR: projectx.exe not found. Run build-windows.bat first.' -ForegroundColor Red
    Write-Host ''
    exit 1
}

Write-Host ''
Write-Host "Packaging mode : $Mode"
Write-Host "Output         : $out"
Write-Host ''

# The output folder is rebuilt from nothing, so it must never be somebody's
# install. A release folder that setup has been run in holds the downloaded
# game data, the Remaster's music and intro and the player's logs; deleting it
# wiped a test install once. Test from dist\ForsakenVR-test (the zip extracted
# and set up), which this script never touches.
if ($Mode -eq 'release' -and (Test-Path (Join-Path $out 'Data'))) {
    Write-Host ''
    Write-Host "ERROR: $out has been set up (it has a Data folder)." -ForegroundColor Red
    Write-Host 'Someone is playing from it. Refusing to delete it.' -ForegroundColor Red
    Write-Host 'Test from dist\ForsakenVR-test instead; move this folder first if it is disposable.' -ForegroundColor Red
    Write-Host ''
    exit 1
}
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out -Force | Out-Null

# A release is an installer: Setup.bat, the readme and the licence at the top,
# the game and the installer's script in files\. Setup puts the game in a VR
# folder of its own (package\setup.ps1). A personal 'full' copy stays the old
# portable folder that runs where it is.
$gameOut = $out
if ($Mode -eq 'release') {
    $gameOut = Join-Path $out 'files\game'
    New-Item -ItemType Directory -Path $gameOut -Force | Out-Null
}

# ---------------------------------------------------------------- executable
Write-Host '  exe'
Copy-Item $exe $gameOut -Force

# --------------------------------------------------------------- runtime DLLs
# Resolved from the binary's own import table rather than hardcoded, so the list
# stays correct when the dependencies change. objdump lives on the build
# machine; if it is missing the walk silently finds nothing, so the count is
# checked below: an archive with no runtime libraries starts fine here, where
# they are still on PATH, and fails on every other machine.
Write-Host '  runtime DLLs'

$objdump = Join-Path $msysBin 'objdump.exe'
if (-not (Test-Path $objdump)) {
    Write-Host ''
    Write-Host "ERROR: objdump.exe not found at $objdump" -ForegroundColor Red
    Write-Host 'Cannot resolve the runtime DLLs. Refusing to build an incomplete folder.' -ForegroundColor Red
    Write-Host ''
    exit 1
}

$seen   = @{}
$queue  = New-Object System.Collections.Queue
$copied = 0
$queue.Enqueue($exe)

while ($queue.Count -gt 0) {
    $f = $queue.Dequeue()
    if (-not (Test-Path $f)) { continue }
    foreach ($line in (& $objdump -p $f | Select-String 'DLL Name:')) {
        $dll = ($line -split 'DLL Name:')[1].Trim()
        $key = $dll.ToLower()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $path = Join-Path $msysBin $dll
        if (Test-Path $path) {
            Copy-Item $path $gameOut -Force
            $copied++
            $queue.Enqueue($path)
        }
    }
}

if ($copied -eq 0) {
    Write-Host ''
    Write-Host 'ERROR: resolved 0 runtime DLLs. That cannot be right.' -ForegroundColor Red
    Write-Host 'Refusing to build a folder that will only run on this machine.' -ForegroundColor Red
    Write-Host ''
    exit 1
}
Write-Host "         $copied copied"

# -------------------------------------------------------------- our own files
Write-Host '  scripts'
robocopy (Join-Path $src 'scripts') (Join-Path $gameOut 'scripts') /e /njh /njs /ndl /nc /ns /np | Out-Null

# Our own level-to-track mapping. Not game data: we wrote it.
$playlist = Join-Path $src 'music\playlist.txt'
if (Test-Path $playlist) {
    New-Item -ItemType Directory -Path (Join-Path $gameOut 'Music') -Force | Out-Null
    Copy-Item $playlist (Join-Path $gameOut 'Music') -Force
}

# configs/ holds the game's settings file, not its assets: key=value preferences
# that the game rewrites on every exit. It is ours to ship, and it is NOT optional:
# AppInit() fails and the process exits 1 before logging is even initialised if
# configs/ is absent, which presents as the game silently doing nothing at all.
Write-Host '  configs'
$cfg = Join-Path $src 'configs'
if (Test-Path $cfg) {
    robocopy $cfg (Join-Path $gameOut 'configs') /e /njh /njs /ndl /nc /ns /np | Out-Null
} else {
    Write-Host ''
    Write-Host 'ERROR: configs/ not found in the source tree.' -ForegroundColor Red
    Write-Host 'The packaged game will not start without it. Refusing to build.' -ForegroundColor Red
    Write-Host ''
    exit 1
}
# A release ships the defaults in package\debug.txt, not this tree's configs\debug.txt,
# which every desk run rewrites with whatever it last tried.
$cfgTemplate = Join-Path $src 'package\debug.txt'
if ($Mode -eq 'release') {
    if (-not (Test-Path $cfgTemplate)) {
        Write-Host 'ERROR: package\debug.txt (the shipped settings) is missing. Refusing to build.' -ForegroundColor Red
        exit 1
    }
    Copy-Item $cfgTemplate (Join-Path $gameOut 'configs\debug.txt') -Force
}

if ($Mode -ne 'release') {
    # Setup makes these in the VR folder; the portable copy needs them here.
    New-Item -ItemType Directory -Path (Join-Path $out 'Pilots')   -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $out 'logs')     -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $out 'savegame') -Force | Out-Null
}

# ------------------------------------------------------------------ game data
if ($Mode -eq 'full') {
    Write-Host '  game data (the slow part, ~300 MB)'
    $p = Join-Path $src 'Data'
    if (Test-Path $p) {
        robocopy $p (Join-Path $out 'Data') /e /njh /njs /ndl /nc /ns /np | Out-Null
    }
    foreach ($pair in @(@('Music', '~105 MB'), @('Movies', '~70 MB'))) {
        $p = Join-Path $src $pair[0]
        if (Test-Path $p) {
            Write-Host ("  {0} ({1})" -f $pair[0], $pair[1])
            robocopy $p (Join-Path $out $pair[0]) /e /njh /njs /ndl /nc /ns /np | Out-Null
        } else {
            Write-Host ("  {0} - not present, skipping" -f $pair[0])
        }
    }
} else {
    Write-Host '  game data: deliberately NOT included (see setup.bat)'

    # The installer: Setup.bat at the top; its script and the two batch files
    # it installs into the VR folder beside the game in files\.
    Write-Host '  installer'
    Copy-Item (Join-Path $src 'LICENSE') $out -Force
    # The shipped libraries' own licences and THIRD-PARTY.txt (which, version, licence, source).
    $lic = Join-Path $out 'licenses'
    New-Item -ItemType Directory -Force $lic | Out-Null
    Copy-Item (Join-Path $src 'package\licenses\*') $lic -Force
    Copy-Item (Join-Path $src 'package\Setup.bat') $out -Force
    foreach ($f in @('setup.ps1', 'Uninstall.bat', 'Collect logs.bat')) {
        Copy-Item (Join-Path $src ('package\' + $f)) (Join-Path $out 'files') -Force
    }
    # Game Or Die's splash, if the art is in the repository.
    $splashArt = Join-Path $src 'art\vrsplash.png'
    if (Test-Path $splashArt) { Copy-Item $splashArt (Join-Path $out 'files') -Force; Write-Host '  splash' }
    # Game Or Die's controls pictures, one per controller, beside the README:
    # art\Controls - Forsaken VR - <device>.png. Our own art, like the splash.
    $controls = @(Get-ChildItem (Join-Path $src 'art') -Filter 'Controls - Forsaken VR - *.png' -ErrorAction SilentlyContinue)
    foreach ($c in $controls) { Copy-Item $c.FullName $out -Force }
    if ($controls.Count) { Write-Host "  controls pictures: $(($controls | ForEach-Object { $_.Name }) -join ', ')" }
    elseif ($Mode -eq 'release') { Write-Host '  NOTE: no controls pictures in art\ (Controls - Forsaken VR - <device>.png)' -ForegroundColor Yellow }

    # There is exactly one README.md in this repo and it is ours: ForsakenX's
    # was replaced rather than kept alongside, so there is no second file that
    # could ship by mistake and no rename step to get wrong. BUILD-WINDOWS.md is
    # also ours but is a developer document, and is deliberately left out: a
    # player does not want build instructions.
    # The content check further down is what actually enforces all of this.
    Write-Host '  readme'
    $readme = Join-Path $src 'README.md'
    if (-not (Test-Path $readme)) {
        Write-Host ''
        Write-Host 'ERROR: README.md not found in the source tree.' -ForegroundColor Red
        Write-Host 'Refusing to build a release with no readme.' -ForegroundColor Red
        Write-Host ''
        exit 1
    }
    Copy-Item $readme $out -Force
}

# ------------------------------------------------------------------ launchers
# A release's launchers are written by Setup, into the VR folder it makes.
if ($Mode -ne 'release') {
Write-Host '  launchers'

$flat = @(
    '@echo off',
    'REM Flat mode. Add -vr for the headset.',
    'pushd "%~dp0"',
    # Name the exe by full path rather than bare. A bare name is resolved from
    # the current directory, which cmd refuses to search when
    # NoDefaultCurrentDirectoryInExePath is set: the launcher then reports
    # "projectx.exe is not recognized" from a folder the exe is sitting in.
    # Not the default on Windows, but it is set in hardened environments, and
    # "%~dp0projectx.exe" costs nothing and cannot be resolved wrongly.
    '"%~dp0projectx.exe" -window %*' 
)
$vr = @(
    '@echo off',
    'REM Headset. Start Virtual Desktop and connect before running.',
    'REM -vrres is a percentage of the runtime''s per-eye resolution.',
    'pushd "%~dp0"',
    '"%~dp0projectx.exe" -window -vr -vrres 100 %*' 
)
Set-Content -Path (Join-Path $out 'Forsaken.bat')    -Value $flat -Encoding ascii
Set-Content -Path (Join-Path $out 'Forsaken VR.bat') -Value $vr   -Encoding ascii
}

# --------------------------------------------------------------- verification
# An allow-list, not a block-list. A block-list of known data extensions is only
# as good as the last time someone remembered to update it; an allow-list fails
# closed when something new and unexpected turns up in the tree.
if ($Mode -eq 'release') {
    Write-Host ''
    Write-Host '  verifying nothing redistributable slipped in'

    $allowed      = @('.exe', '.dll', '.bat', '.ps1', '.lua', '.md', '.txt')
    $allowedNames = @('LICENSE', 'vrsplash.png')   # the splash is our own art
    $bad = @()
    foreach ($f in (Get-ChildItem $out -Recurse -File)) {
        if ($allowed -contains $f.Extension.ToLower()) { continue }
        if ($allowedNames -contains $f.Name) { continue }
        if ($f.Name -like 'Controls - Forsaken VR - *.png' -and $f.DirectoryName -eq $out) { continue }   # our controls pictures
        $bad += $f.FullName.Substring($out.Length + 1)
    }

    if ($bad.Count -gt 0) {
        Write-Host ''
        Write-Host 'REFUSING TO PUBLISH. Unexpected files in the release folder:' -ForegroundColor Red
        foreach ($b in $bad) { Write-Host "    $b" -ForegroundColor Red }
        Write-Host ''
        Write-Host 'Game data is never ours to distribute. If one of these is genuinely' -ForegroundColor Red
        Write-Host 'our own work, add its extension to $allowed in this script.' -ForegroundColor Red
        Write-Host ''
        Remove-Item $out -Recurse -Force
        exit 1
    }
    Write-Host ("         {0} files, all accounted for" -f (Get-ChildItem $out -Recurse -File).Count)

    # The readme is the first thing a player opens, so check it is ours by its
    # content rather than trusting that the copy above did what it looked like.
    Write-Host '  verifying the readme is ours'
    $docs = @(Get-ChildItem $out -Recurse -File -Filter *.md)
    $rm = Join-Path $out 'README.md'
    $docErr = @()
    if (-not (Test-Path $rm)) {
        $docErr += 'README.md is missing from the release.'
    } else {
        $text = Get-Content $rm -Raw
        if ($text -notmatch '(?m)^#\s+Forsaken VR\b') {
            $docErr += 'README.md does not start with our own title.'
        }
        if ($text -match 'This repo holds the community port') {
            $docErr += "README.md is ForsakenX's, not ours."
        }
    }
    foreach ($d in $docs) {
        if ($d.Name -ne 'README.md') {
            $docErr += ("extra document in the release: {0}" -f $d.FullName.Substring($out.Length + 1))
        }
    }
    if ($docErr.Count -gt 0) {
        Write-Host ''
        Write-Host 'REFUSING TO PUBLISH. Documentation problems:' -ForegroundColor Red
        foreach ($e in $docErr) { Write-Host "    $e" -ForegroundColor Red }
        Write-Host ''
        Write-Host 'A player opening someone else''s readme cannot tell whose build this is.' -ForegroundColor Red
        Write-Host ''
        Remove-Item $out -Recurse -Force
        exit 1
    }
    Write-Host '         README.md is ours, and it is the only document'
}

# ----------------------------------------------------------------- the zip
# Release only. A `full` package holds the soundtrack and the intro movie,
# which are Nightdive's, so it must never be turned into something uploadable.
#
# The script used to stop at a folder, and a stale ForsakenVR.zip from an
# earlier build sat next to it for three weeks looking like the current
# release. Building the archive here means the zip cannot lag the folder.
$zip = $null
if ($Mode -eq 'release') {
    Write-Host ''
    Write-Host '  building the archive'

    # Stage under the name players should see when they extract it, rather
    # than the internal '-release' working directory name.
    $stage = Join-Path $src 'dist\_zipstage'
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item $out (Join-Path $stage 'Forsaken-VR') -Recurse -Force

    # The release name, version from Setup: Forsaken-VR-<version>-PCVR.zip.
    $ver = ([regex]::Match((Get-Content (Join-Path $src 'package\setup.ps1') -Raw), "\`$Version = '([^']+)'")).Groups[1].Value
    if (-not $ver) { throw 'no $Version in package\setup.ps1' }
    $zip = Join-Path $src ('dist\Forsaken-VR-' + $ver + '-PCVR.zip')
    if (Test-Path $zip) { Remove-Item $zip -Force }

    # Invoke-WebRequest is not the only cmdlet that redraws a progress bar per
    # chunk; Compress-Archive does it too and is far slower with it on.
    $oldProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    Compress-Archive -Path (Join-Path $stage 'Forsaken-VR') -DestinationPath $zip
    $ProgressPreference = $oldProgress

    Remove-Item $stage -Recurse -Force

    # Verify by reading the archive back, not by trusting the cmdlet. An
    # archive that is short a file still compresses without complaint.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $expected = (Get-ChildItem $out -Recurse -File).Count
    $archive  = [System.IO.Compression.ZipFile]::OpenRead($zip)
    $actual   = @($archive.Entries | Where-Object { $_.Name -ne '' }).Count
    $archive.Dispose()

    if ($actual -ne $expected) {
        Write-Host ''
        Write-Host ("ARCHIVE IS WRONG: {0} files packaged, {1} in the zip." -f $expected, $actual) -ForegroundColor Red
        Write-Host ''
        Remove-Item $zip -Force
        exit 1
    }
    Write-Host ("         {0} files verified inside the zip" -f $actual)
}

# ------------------------------------------------------------------ finish up
$size = (Get-ChildItem $out -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ''
Write-Host ('Done. {0:N1} MB in {1}' -f ($size / 1MB), $out)
if ($Mode -eq 'release') {
    if ($zip) {
        $zsize = (Get-Item $zip).Length
        Write-Host ('      {0:N1} MB zipped to {1}' -f ($zsize / 1MB), $zip)
    }
    Write-Host 'Ship the zip. The player unzips it and runs Setup.bat.'
} else {
    Write-Host 'Personal copy only. It contains game data and must not be uploaded.'
}
Write-Host ''

# robocopy exits 1 for "files were copied", and PowerShell hands the last native
# exit code to the caller. Without this the launcher reports a successful
# package as a failure.
exit 0
