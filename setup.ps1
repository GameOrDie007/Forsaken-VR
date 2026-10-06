<#
    One-time setup for the Forsaken VR release folder.

    This release ships our code and nothing else: about 20 MB. Everything the
    game needs to run is assembled here, on your machine, from sources that are
    free to give it to you:

      Data/     downloaded from the ForsakenX data set on GitHub
      Music/    copied from your own Forsaken Remastered install, if you have it
      Movies/   copied from your own Forsaken Remastered install, if you have it

    Nothing is redistributed by us. Music and the intro movie are optional; the
    game plays without them.

    Usage:
      .\setup.ps1                          the normal case, no arguments needed
      .\setup.ps1 -DataPath "X:\forsaken-data"   use a copy you already have
      .\setup.ps1 -RemasterPath "X:\Forsaken Remastered"
      .\setup.ps1 -NoDownload              never touch the network
      .\setup.ps1 -OriginalTextures        also install the original 1998 art, so
                                           UseOriginalTextures in the config works
      .\setup.ps1 -BonusLevels             also install the levels Forsaken
                                           Remastered has and this data set does
                                           not, including the N64 exclusives
      .\setup.ps1 -Force                   redo steps that are already done

    A folder dropped on setup.bat is recognised by what is in it: a copy of
    the data set (it has a levels folder) or a Forsaken Remastered install.
#>
# Positional binding off: a folder dropped on setup.bat arrives as a bare
# argument, and with ordinary parameters it would bind to whichever came first.
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DataPath = '',
    [string]$RemasterPath = '',
    [switch]$NoDownload,
    [switch]$OriginalTextures,
    [switch]$BonusLevels,
    [switch]$Force,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Dropped = @()
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

$DATA_URL  = 'https://github.com/ForsakenX/forsaken-data/archive/refs/heads/master.zip'
$DATA_REPO = 'https://github.com/ForsakenX/forsaken-data'

# Written line by line from the start, beside this script, so a run that stops
# half way (or one on a network folder nobody is watching) still says where.
$setupLog = Join-Path $here 'setup-log.txt'
try { Set-Content -Path $setupLog -Value ("Forsaken VR setup, " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + " on $env:COMPUTERNAME, in $here") -Encoding ascii } catch { }
function Log($m)  { try { Add-Content -Path $setupLog -Value $m -Encoding ascii } catch { } }

function Say($m)  { Write-Host $m;                                   Log $m }
function Ok($m)   { Write-Host "  OK   $m"   -ForegroundColor Green;  Log "  OK   $m" }
function Warn($m) { Write-Host "  --   $m"   -ForegroundColor Yellow; Log "  --   $m" }
function Bad($m)  { Write-Host "  !!   $m"   -ForegroundColor Red;    Log "  !!   $m" }

trap {
    Bad "setup stopped: $($_.Exception.Message)"
    Log ($_.ScriptStackTrace)
    exit 1
}

Say ''
Say 'Forsaken VR -- setup'
Say '===================='
Say ''

foreach ($d in $Dropped) {
    if (-not $d) { continue }
    $d = $d.Trim('"')
    if (-not (Test-Path -LiteralPath $d -PathType Container)) {
        Warn "ignoring '$d' -- not a folder"
        continue
    }
    if (Test-Path -LiteralPath (Join-Path $d 'levels')) {
        $DataPath = $d;     Say "  using the data set you dropped: $d"
    } elseif ((Test-Path -LiteralPath (Join-Path $d 'ForsakenEX.kpf')) -or
              (Test-Path -LiteralPath (Join-Path $d 'music\OGG'))) {
        $RemasterPath = $d; Say "  using the Forsaken Remastered you dropped: $d"
    } else {
        Warn "'$d' is neither the data set nor Forsaken Remastered -- ignored"
    }
}

# =========================================================== locating the game
# Steam records its own location in the registry, and the libraries it manages
# on other drives in steamapps\libraryfolders.vdf. GOG registers each game
# separately. Check both: a player who owns it in one place should never have
# to type a path.
function Get-SteamRoots {
    $roots = @()
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        try {
            $p = Get-ItemProperty -Path $k -ErrorAction Stop
            foreach ($v in @($p.SteamPath, $p.InstallPath)) {
                if ($v) { $roots += $v.Replace('/', '\') }
            }
        } catch { }
    }
    $extra = @()
    foreach ($r in $roots) {
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($line in (Get-Content $vdf)) {
                if ($line -match '"path"\s+"(.+?)"') { $extra += $matches[1].Replace('\\', '\') }
            }
        }
    }
    return (@($roots + $extra) | Sort-Object -Unique)
}

function Find-Remaster {
    foreach ($r in (Get-SteamRoots)) {
        $p = Join-Path $r 'steamapps\common\Forsaken Remastered'
        if (Test-Path $p) { return $p }
    }
    # GOG
    foreach ($base in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games')) {
        if (-not (Test-Path $base)) { continue }
        foreach ($k in (Get-ChildItem $base -ErrorAction SilentlyContinue)) {
            try {
                $p = Get-ItemProperty -Path $k.PSPath -ErrorAction Stop
                if ($p.gameName -and $p.gameName -match 'Forsaken' -and $p.path -and (Test-Path $p.path)) {
                    return $p.path
                }
            } catch { }
        }
    }
    return ''
}

if ($RemasterPath -eq '') { $RemasterPath = Find-Remaster }
$haveRemaster = ($RemasterPath -ne '') -and (Test-Path $RemasterPath)

# ================================================================== game data
Say 'Game data'
Say '---------'
$dstData = Join-Path $here 'Data'
$dataReady = Test-Path (Join-Path $dstData 'levels')

if ($dataReady -and -not $Force) {
    Ok 'already installed'
} else {
    # A copy the player already has always beats a download.
    if ($DataPath -eq '') {
        foreach ($guess in @((Join-Path $here '..\forsaken-data'),
                             (Join-Path $here '..\data'),
                             (Join-Path $here '..\ForsakenX\data'))) {
            # ProviderPath, not Path: on a network folder .Path is
            # "Microsoft.PowerShell.Core\FileSystem::\\server\...", which
            # robocopy and .NET refuse.
            if (Test-Path (Join-Path $guess 'levels')) { $DataPath = (Resolve-Path $guess).ProviderPath; break }
        }
    }

    if ($DataPath -ne '' -and (Test-Path (Join-Path $DataPath 'levels'))) {
        Say "  copying from $DataPath"
        Say '  (about 175 MB, takes a minute)'
        robocopy $DataPath $dstData /e /njh /njs /ndl /nc /ns /np | Out-Null
        if (Test-Path (Join-Path $dstData 'levels')) { Ok 'installed' } else { Bad 'copy failed' }
    }
    elseif ($NoDownload) {
        Bad 'not found, and -NoDownload was given.'
        Say "       Get it from $DATA_REPO"
    }
    else {
        Say '  not found locally -- downloading the ForsakenX data set'
        Say "    from $DATA_REPO"
        Say '    about 175 MB. This is the only thing setup downloads.'
        Say ''

        $tmpZip = Join-Path $env:TEMP ('forsaken-data-' + [Guid]::NewGuid().ToString('N') + '.zip')
        $tmpDir = Join-Path $env:TEMP ('forsaken-data-' + [Guid]::NewGuid().ToString('N'))
        $downloaded = $false
        try {
            # Some older systems still default to TLS 1.0, which GitHub refuses.
            [Net.ServicePointManager]::SecurityProtocol =
                [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            # Invoke-WebRequest renders a progress bar per chunk in PowerShell 5.1,
            # which makes a large download several times slower than it should be.
            $oldProgress = $ProgressPreference
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $DATA_URL -OutFile $tmpZip -UseBasicParsing -TimeoutSec 900
            $ProgressPreference = $oldProgress
            $downloaded = $true
        } catch {
            Bad "download failed: $($_.Exception.Message)"
            Say ''
            Say "       Download it yourself from $DATA_REPO"
            Say '       (the green Code button, then Download ZIP), unpack it, and re-run:'
            Say '         .\setup.ps1 -DataPath "X:\path\to\forsaken-data"'
        }

        if ($downloaded) {
            $sz = (Get-Item $tmpZip).Length
            Say ("  downloaded {0:N1} MB" -f ($sz / 1MB))
            try {
                Add-Type -AssemblyName System.IO.Compression.FileSystem
                [System.IO.Compression.ZipFile]::ExtractToDirectory($tmpZip, $tmpDir)
                # GitHub archives nest everything under <repo>-<branch>/.
                $inner = Get-ChildItem $tmpDir -Directory | Select-Object -First 1
                $root = $tmpDir
                if ($inner -and (Test-Path (Join-Path $inner.FullName 'levels'))) { $root = $inner.FullName }
                if (-not (Test-Path (Join-Path $root 'levels'))) {
                    Bad 'the archive did not contain a levels folder -- layout may have changed upstream.'
                } else {
                    Say '  installing'
                    robocopy $root $dstData /e /njh /njs /ndl /nc /ns /np | Out-Null
                    if (Test-Path (Join-Path $dstData 'levels')) { Ok 'installed' } else { Bad 'install failed' }
                }
            } catch {
                Bad "could not unpack the archive: $($_.Exception.Message)"
            }
        }
        foreach ($t in @($tmpZip, $tmpDir)) {
            if (Test-Path $t) { Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue }
        }
    }
}
Say ''

# ==================================================== soundtrack and the intro
Say 'Soundtrack and intro movie'
Say '--------------------------'
if ($haveRemaster) {
    Say '  found Forsaken Remastered at:'
    Say "    $RemasterPath"

    $srcMusic = Join-Path $RemasterPath 'music\OGG'
    if (Test-Path $srcMusic) {
        $dstMusic = Join-Path $here 'Music\OGG'
        New-Item -ItemType Directory -Path $dstMusic -Force | Out-Null
        $n = 0
        foreach ($f in (Get-ChildItem $srcMusic -File -Filter *.ogg)) {
            Copy-Item $f.FullName $dstMusic -Force; $n++
        }
        if ($n -gt 0) { Ok "$n music tracks" } else { Warn 'no .ogg files there' }
    } else { Warn 'no music folder in that install' }

    $srcIntro = Join-Path $RemasterPath 'movies\intro.ogv'
    if (Test-Path $srcIntro) {
        $dstMovies = Join-Path $here 'Movies'
        New-Item -ItemType Directory -Path $dstMovies -Force | Out-Null
        Copy-Item $srcIntro $dstMovies -Force
        Ok 'intro movie'
    } else { Warn 'no intro.ogv in that install' }
} else {
    Warn 'Forsaken Remastered not found -- skipping music and the intro movie.'
    Say  '       Both are optional. If you own it somewhere Steam and GOG do not'
    Say  '       know about, re-run with:'
    Say  '         .\setup.ps1 -RemasterPath "X:\path\to\Forsaken Remastered"'
}
Say ''

# =============================================== optional: the original 1998 art
# The engine looks in Data\textures\original\ first when UseOriginalTextures is
# set in the config, and the data set does not ship that folder. The Remaster
# carries the original art, so we can build it from the player's own copy and
# give them a toggle between the two.
if ($OriginalTextures) {
    Say 'Original textures'
    Say '-----------------'
    if (-not $haveRemaster) {
        Warn 'needs Forsaken Remastered, which was not found -- skipped'
    } else {
        $kpf = Join-Path $RemasterPath 'ForsakenEX.kpf'
        if (-not (Test-Path $kpf)) {
            Warn 'ForsakenEX.kpf not found in that install -- skipped'
        } else {
            $dstOrig = Join-Path $dstData 'textures\original'
            New-Item -ItemType Directory -Path $dstOrig -Force | Out-Null
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            Add-Type -AssemblyName System.Drawing
            $zip = [System.IO.Compression.ZipFile]::OpenRead($kpf)
            $n = 0; $failed = 0
            try {
                foreach ($e in $zip.Entries) {
                    if ($e.FullName -notlike 'textures/*') { continue }
                    if ([IO.Path]::GetExtension($e.Name).ToLower() -ne '.bmp') { continue }
                    $tmpBmp = Join-Path $env:TEMP ('fsx_' + $e.Name)
                    $png = Join-Path $dstOrig ([IO.Path]::ChangeExtension($e.Name, '.png').ToLower())
                    try {
                        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $tmpBmp, $true)
                        $img = [System.Drawing.Image]::FromFile($tmpBmp)
                        $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
                        $img.Dispose()
                        $n++
                    } catch { $failed++ }
                    if (Test-Path $tmpBmp) { Remove-Item $tmpBmp -Force -ErrorAction SilentlyContinue }
                }
            } finally { $zip.Dispose() }
            # The Remaster does not carry an original for every texture the game
            # uses. That is fine: the engine falls through to data\textures\
            # for anything missing here, but say so rather than implying the
            # switch gives you a wholly 1998 look.
            $total = 0
            $texDir = Join-Path $dstData 'textures'
            if (Test-Path $texDir) { $total = (Get-ChildItem $texDir -File -Filter *.png).Count }
            if ($n -gt 0) {
                if ($total -gt 0) { Ok "$n of $total textures replaced with the 1998 originals" }
                else { Ok "$n original textures converted" }
            } else { Bad 'converted nothing' }
            if ($failed -gt 0) { Warn "$failed could not be converted" }
            Say '       Set UseOriginalTextures = true in the config to use them.'
            Say '       Anything without an original keeps the high-resolution art.'
        }
    }
    Say ''
}

# ================================================ optional: the bonus levels
# Forsaken Remastered ships levels the community data set does not have,
# including the N64 version's exclusives. They are the SAME format: Nightdive
# kept the original .bsp/.mxv/.mis files, PRJX version 1, which this engine
# reads directly, so they load as they are, from the player's own install.
#
# Measured before this was written: all of them load and render in this build.
# Two caveats, both stated to the player rather than hidden:
#   - none carry a .GOL, so they cannot be used for Capture The Flag. GoalLoad()
#     only reads that file when CTF is on and succeeds when it is absent, so
#     nothing else is affected.
#   - some carry no .STP, so LoadStartPoints() finds nothing and the ship starts
#     at the origin rather than a designed spawn. The level is still flyable.
#
# They go in battle.dat, not mission.dat: the campaign has end-of-game logic
# keyed to the length of its list, and appending to it would change where the
# campaign ends.
if ($BonusLevels) {
    Say 'Bonus levels from Forsaken Remastered'
    Say '-------------------------------------'
    if (-not $haveRemaster) {
        Warn 'needs Forsaken Remastered, which was not found -- skipped'
    } else {
        $kpf = Join-Path $RemasterPath 'ForsakenEX.kpf'
        if (-not (Test-Path $kpf)) {
            Warn 'ForsakenEX.kpf not found in that install -- skipped'
        } else {
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            Add-Type -AssemblyName System.Drawing
            $zip = [System.IO.Compression.ZipFile]::OpenRead($kpf)
            $levelsDir = Join-Path $dstData 'levels'
            $added = @(); $files = 0; $converted = 0; $noStart = @()

            try {
                # A level exists, as far as this engine is concerned, when its
                # .mxv is present: that is the file InitLevels() tests for.
                # Use the same rule rather than a hardcoded list, so this keeps
                # working if either side gains levels later.
                $candidates = @()
                foreach ($e in $zip.Entries) {
                    if ($e.FullName -match '^levels/([^/]+)/\1\.mxv$') {
                        $candidates += $Matches[1]
                    }
                }
                $candidates = $candidates | Sort-Object -Unique

                foreach ($lv in $candidates) {
                    $have = Join-Path $levelsDir (Join-Path $lv "$lv.mxv")
                    if (Test-Path -LiteralPath $have) { continue }   # already ours

                    $prefix = "levels/$lv/"
                    foreach ($e in $zip.Entries) {
                        if (-not $e.FullName.StartsWith($prefix)) { continue }
                        if ($e.Name -eq '') { continue }             # directory
                        # skip the localised copies of the .mis
                        $rel = $e.FullName.Substring('levels/'.Length)
                        $parts = $rel.Split('/')
                        if ($parts.Count -gt 2 -and
                            @('de','es','fr','it') -contains $parts[1]) { continue }

                        $dest = [System.IO.Path]::Combine($levelsDir, ($parts -join '\'))
                        [System.IO.Directory]::CreateDirectory(
                            [System.IO.Path]::GetDirectoryName($dest)) | Out-Null
                        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
                        $files++

                        # this build reads PNG; the remaster ships the original BMPs
                        if ([IO.Path]::GetExtension($dest).ToLower() -eq '.bmp') {
                            try {
                                $img = [System.Drawing.Image]::FromFile($dest)
                                $img.Save([IO.Path]::ChangeExtension($dest, '.png'),
                                          [System.Drawing.Imaging.ImageFormat]::Png)
                                $img.Dispose()
                                $converted++; $files++
                            } catch { }
                        }
                    }
                    $added += $lv
                    if (-not (Test-Path -LiteralPath (Join-Path $levelsDir (Join-Path $lv "$lv.stp")))) {
                        $noStart += $lv
                    }
                }
            } finally { $zip.Dispose() }

            if ($added.Count -eq 0) {
                Warn 'no levels there that the data set does not already have'
            } else {
                # Append to battle.dat, skipping anything already listed so a
                # second run is harmless.
                $battle = Join-Path $levelsDir 'battle.dat'
                $existing = @()
                if (Test-Path $battle) {
                    $existing = @(Get-Content $battle | ForEach-Object { $_.Trim() } |
                                  Where-Object { $_ -ne '' })
                }
                $new = @($added | Where-Object { $existing -notcontains $_ })

                # MAXLEVELS is 64 in main.h; the engine stops reading there.
                $room = 64 - $existing.Count
                if ($new.Count -gt $room) {
                    Warn ("only room for {0} more levels (engine limit is 64) -- adding the first {0}" -f $room)
                    $new = $new[0..([Math]::Max($room - 1, 0))]
                }

                if ($new.Count -gt 0) {
                    Add-Content -Path $battle -Value $new -Encoding ascii
                }
                Ok ("{0} bonus levels, {1} files, {2} textures converted" -f $added.Count, $files, $converted)
                Say ("       added to the multiplayer level list: {0}" -f ($new -join ', '))
                if ($noStart.Count -gt 0) {
                    Say ''
                    Warn ("{0} have no start point and begin at the origin:" -f $noStart.Count)
                    Say  ("       {0}" -f ($noStart -join ', '))
                }
                Say '       None of them support Capture The Flag (no .GOL file).'
            }
        }
    }
    Say ''
}

# ==================================================================== verdict
Say 'Result'
Say '------'
$haveData   = Test-Path (Join-Path $here 'Data\levels')
$haveMusic  = Test-Path (Join-Path $here 'Music\OGG')
$haveMovies = Test-Path (Join-Path $here 'Movies\intro.ogv')

if ($haveData)   { Ok   'game data    -- ready' } else { Bad  'game data    -- MISSING, see above' }
if ($haveMusic)  { Ok   'soundtrack   -- ready' } else { Warn 'soundtrack   -- absent (optional)' }
if ($haveMovies) { Ok   'intro movie  -- ready' } else { Warn 'intro movie  -- absent (optional)' }

$battleFile = Join-Path $here 'Data\levels\battle.dat'
if (Test-Path $battleFile) {
    $battleCount = @(Get-Content $battleFile | Where-Object { $_.Trim() -ne '' }).Count
    if ($battleCount -gt 38) {
        Ok ("bonus levels -- {0} extra in the multiplayer list" -f ($battleCount - 38))
    }
}

Say ''
if ($haveData) {
    Say 'Setup complete.'
    Say '  "Forsaken VR.bat"  headset -- start Virtual Desktop and connect first'
    Say '  Forsaken.bat       flat, on the monitor'
    exit 0
} else {
    Say 'Setup incomplete -- the game data is required.'
    exit 1
}
