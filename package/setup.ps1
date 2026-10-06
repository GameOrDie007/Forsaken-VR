# Forsaken VR: install, uninstall and log collection, in one script.
#
# It finds YOUR copy of Forsaken Remastered (every Steam library, GOG, the
# Windows list of installed programs, the usual folders, or a folder you drag
# in) and makes a VR folder inside its folder. Forsaken VR does not need the
# Remaster: it plays on the free ForsakenX data set, which this downloads
# from the ForsakenX project, but if you own it, its soundtrack and intro
# film are used, and the VR folder lives beside it. Without it the VR folder
# goes in Documents\Forsaken VR, or, when OneDrive keeps Documents, in
# %USERPROFILE%\Games\Forsaken VR. No game data ships with this download.
#
#   Setup.bat           install (or update): this script with no switch
#   Uninstall.bat       -Uninstall: removes everything it installed; your
#                       pilot, saved games and settings are kept
#   Collect logs.bat    -Collect: zips the logs onto your desktop to send us
#
# Safe to run again at any time. It never overwrites a pilot, a saved game or
# a setting, and an update keeps the data it already fetched.
#
# Optional extras from your own Forsaken Remastered:
#   -BonusLevels        its extra levels (the N64 ones too) for multiplayer
#   -OriginalTextures   the 1998 art, for UseOriginalTextures in the settings
#   -DataPath <folder>  a copy of the ForsakenX data set you already have
#   -NoDownload         never use the network
#
# For tools and hubs (the same switches in every Game Or Die port):
#   -Quiet              no questions, no pause, no Explorer window
#   -GamePath <folder>  install for this Forsaken Remastered folder; several
#                       separated by ';'; no searching
#   -Json               print the result as JSON on stdout (nothing else)
#   -Detect             change nothing; report the game and the install
#   -AddToSteam         with -Quiet: add the Steam library entry
#   -CloseSteam         with -Quiet: close Steam to write it (restarted after)
#   -NoSteam, -NoShortcuts   skip the Steam entry / the desktop shortcut
# Exit codes: 0 done, 1 nothing could be installed, 2 error.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch] $Uninstall,
    [switch] $Collect,
    [switch] $Detect,
    [switch] $Quiet,
    [switch] $Json,
    [string[]] $GamePath,
    [switch] $AddToSteam,
    [switch] $CloseSteam,
    [switch] $NoSteam,
    [switch] $NoShortcuts,
    [string] $DataPath = '',
    [switch] $NoDownload,
    [switch] $BonusLevels,
    [switch] $OriginalTextures,
    # Tests: no questions; only the folders given; a stand-in Steam folder;
    # a stand-in Documents folder; a stand-in user folder (%USERPROFILE%).
    [switch] $NoPrompt,
    [switch] $NoSearch,
    [string] $SteamRoot,
    [string] $DocumentsRoot,
    [string] $UserRoot,
    # Folders dropped on Setup.bat arrive as bare arguments; collected here,
    # so none is bound to a switch by position.
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Dropped
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Product = 'Forsaken VR'
$ProductFile = 'Forsaken VR'
$Version = '1.0'
$Title = 'Forsaken VR'
# GameExe, not Exe: PowerShell names are not case-sensitive, and a function's
# local $exe would hide it.
$GameExe = 'projectx.exe'
$VRArgs = '-window -vr -vrres 100 -log'
$Launcher = 'Forsaken VR.cmd'
$Flat = 'Forsaken (flat).cmd'
$ShortcutName = 'Forsaken VR.lnk'
$DATA_URL  = 'https://github.com/ForsakenX/forsaken-data/archive/refs/heads/master.zip'
$DATA_REPO = 'https://github.com/ForsakenX/forsaken-data'

if ($Quiet -or $Json) { $NoPrompt = $true }
if ($GamePath) { $NoSearch = $true; $Dropped = @($Dropped) + @($GamePath | ForEach-Object { $_ -split ';' } | Where-Object { $_ }) }

# ProviderPath, not Path: on a network share .Path carries a PowerShell
# provider prefix that .NET file calls refuse.
$Here = (Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath
# Run from the unzipped download, the game sits in files\game beside this
# script; run from an installed VR folder (Uninstall, Collect logs), this
# script sits in VR\tools.
$Package = $null
if (Test-Path -LiteralPath (Join-Path $Here ('game\' + $GameExe))) { $Package = $Here }

# The player's own, in VR\game: never removed, never overwritten.
$PlayerFiles = @('Player.txt', 'configs\debug.txt')
$PlayerDirs  = @('pilots', 'savegame')

# --- the log --------------------------------------------------------------------

$Mode = 'install'
if ($Uninstall) { $Mode = 'uninstall' }
if ($Collect)   { $Mode = 'collect' }
if ($Detect)    { $Mode = 'detect' }
$LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-' + $Mode + '.txt')
if ($Mode -eq 'install' -and $Package) { $LogFile = Join-Path (Split-Path -Parent $Here) 'setup-log.txt' }

$Result = [ordered]@{ product = $Product; version = $Version; mode = $Mode; result = 'ok' }

function Say([string] $m, [string] $color) {
    if ($Json) { return }	# stdout carries the JSON alone
    if ($color) { Write-Host $m -ForegroundColor $color } else { Write-Host $m }
}
function Log([string] $m) {
    # Line by line from the start, so a failure part-way still leaves a log.
    Add-Content -LiteralPath $LogFile -Value $m -Encoding UTF8
    Say $m
}
try { Set-Content -LiteralPath $LogFile -Value ($Product + ' ' + $Version + ' ' + $Mode + ', ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' on ' + $env:COMPUTERNAME) -Encoding UTF8 }
catch { $LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-' + $Mode + '.txt'); Set-Content -LiteralPath $LogFile -Value ($Product + ' ' + $Mode) -Encoding UTF8 }

function Finish([int] $code) {
    if ($Json) {
        $Result['exitCode'] = $code
        $Result['game'] = Game-Status
        Write-Output ($Result | ConvertTo-Json -Depth 5)
    }
    exit $code
}
trap {
    Log ''
    Log ('ERROR: ' + $_.Exception.Message)
    Log ('  at ' + $_.InvocationInfo.PositionMessage)
    Log ''
    Log 'Setup stopped.  Run "Collect logs.bat" and send us the file it makes.'
    $Result['result'] = 'error'
    $Result['error'] = $_.Exception.Message
    if ($Json) { $Result['exitCode'] = 2; Write-Output ($Result | ConvertTo-Json -Depth 5) }
    exit 2
}

function Banner([string] $what) {
    Say ''
    Say ('  ' + $Product + ' ' + $Version + ': ' + $what) 'Cyan'
    Say ('  ' + ('-' * ($Product.Length + $Version.Length + $what.Length + 3)))
    Say ''
}

function Ask-YesNo([string] $q) {
    # Yes unless the player says no; no questions at all under -Quiet.
    if ($NoPrompt) { return $false }
    try { if ([Console]::IsInputRedirected) { return $false } } catch { }
    $a = Read-Host ('  ' + $q + ' [Y/n]')
    if ($null -eq $a) { return $false }
    return -not ($a.Trim() -match '^(n|no)$')
}

function Documents {
    if ($DocumentsRoot) { return $DocumentsRoot }
    return [Environment]::GetFolderPath('MyDocuments')
}
# Where the VR folder goes when the Remaster's folder will not do: Documents,
# unless OneDrive keeps it (the default on many PCs), then the game, its data
# and the saves would all be uploaded, and a file OneDrive is busy with can
# stall the game. %USERPROFILE%\Games instead.
function VR-Home {
    $d = Documents
    $user = $env:USERPROFILE
    if ($UserRoot) { $user = $UserRoot }
    # a OneDrive folder in the path ("OneDrive", "OneDrive - <company>"), or the
    # folder Windows says OneDrive syncs, not just any path with the word in it
    $synced = $d -match '(^|\\)OneDrive( - [^\\]*)?(\\|$)'
    foreach ($od in @($env:OneDrive, $env:OneDriveConsumer, $env:OneDriveCommercial)) {
        if ($od -and -not $DocumentsRoot -and $d.StartsWith($od.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { $synced = $true }
    }
    if ($synced) { return (Join-Path $user 'Games') }
    return $d
}
function Desktop { return [Environment]::GetFolderPath('Desktop') }

# --- finding Forsaken Remastered -------------------------------------------------------

function True-Case([string] $p) {
    # The folder's name as it is on disk: Steam reports its path in lower case.
    try {
        $d = New-Object System.IO.DirectoryInfo $p
        if (-not $d.Exists) { return $p }
        if (-not $d.Parent) { return $d.FullName.ToUpper() }
        $match = $d.Parent.GetDirectories($d.Name) | Select-Object -First 1
        if (-not $match) { return $p }
        return (Join-Path (True-Case $d.Parent.FullName) $match.Name)
    } catch { return $p }
}

$Roots = New-Object System.Collections.Generic.List[string]
function Add-Root([string] $p) {
    if (-not $p) { return }
    try { $p = [System.IO.Path]::GetFullPath($p.Trim().Trim('"').TrimEnd('\')) } catch { return }
    $p = True-Case $p
    # The registry and Steam's own file spell the same library in different
    # case: compare without case, or every library is searched twice.
    foreach ($r in $Roots) { if ($r -ieq $p) { return } }
    if (Test-Path -LiteralPath $p -PathType Container) { $Roots.Add($p) }
}

function Steam-Dir {
    if ($SteamRoot) { return $SteamRoot }
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop
            foreach ($p in @($v.SteamPath, $v.InstallPath)) {
                if ($p -and (Test-Path -LiteralPath ($p -replace '/', '\'))) { return (True-Case ($p -replace '/', '\')) }
            }
        } catch { }
    }
    return $null
}

$Searched = $false
function Search-Roots {
    if ($Searched) { return }
    $script:Searched = $true
    foreach ($d in $Dropped) {
        if (-not $d) { continue }
        $d = $d.Trim('"')
        # A dropped copy of the data set is the data, not a game folder.
        if (Test-Path -LiteralPath (Join-Path $d 'levels')) { $script:DataPath = $d; continue }
        Add-Root $d
    }
    if ($NoSearch) { return }

    # 1-2. Steam, from its own registry entry, and every library it lists.
    $steamDirs = @()
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop
            if ($v.SteamPath)   { $steamDirs += $v.SteamPath }
            if ($v.InstallPath) { $steamDirs += $v.InstallPath }
        } catch { }
    }
    $libraries = @()
    foreach ($s in $steamDirs) {
        $s = $s -replace '/', '\'
        $libraries += $s
        foreach ($vdf in @((Join-Path $s 'steamapps\libraryfolders.vdf'), (Join-Path $s 'config\libraryfolders.vdf'))) {
            if (-not (Test-Path -LiteralPath $vdf)) { continue }
            foreach ($line in Get-Content -LiteralPath $vdf) {
                if ($line -match '"path"\s+"([^"]+)"') { $libraries += ($Matches[1] -replace '\\\\', '\') }
            }
        }
    }
    foreach ($lib in $libraries) { Add-Root (Join-Path $lib 'steamapps\common\Forsaken Remastered') }

    # 3. GOG registers each game on its own.
    foreach ($k in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games')) {
        try {
            foreach ($e in Get-ChildItem -LiteralPath $k -ErrorAction Stop) {
                $p = Get-ItemProperty -LiteralPath $e.PSPath
                if ($p.gameName -match 'Forsaken' -and $p.path) { Add-Root $p.path }
            }
        } catch { }
    }

    # 4. The Windows list of installed programs: other stores.
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                     'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
                     'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall')) {
        try {
            foreach ($e in Get-ChildItem -LiteralPath $k -ErrorAction Stop) {
                $p = Get-ItemProperty -LiteralPath $e.PSPath
                if ($p.DisplayName -match 'Forsaken' -and $p.InstallLocation) { Add-Root $p.InstallLocation }
            }
        } catch { }
    }

    # 5. The usual folders on every drive, last.
    $drives = @()
    try { $drives = [System.IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq 'Fixed' -and $_.IsReady } | ForEach-Object { $_.RootDirectory.FullName } } catch { $drives = @('C:\') }
    foreach ($dr in $drives) {
        foreach ($f in @('Forsaken Remastered', 'Forsaken')) {
            Add-Root (Join-Path $dr ('GOG Games\' + $f))
            Add-Root (Join-Path $dr ('GOG Galaxy\Games\' + $f))
            Add-Root (Join-Path $dr ('Games\' + $f))
        }
    }
}

# Forsaken Remastered's folder holds ForsakenEX.kpf.
function Find-Remaster {
    foreach ($r in $Roots) {
        if (Test-Path -LiteralPath (Join-Path $r 'ForsakenEX.kpf')) { return $r }
    }
    return $null
}

# The drag-and-drop fallback.
function Ask-Folder {
    if ($NoPrompt) { return $null }
    try { if ([Console]::IsInputRedirected) { return $null } } catch { }
    Say ''
    Say '  Forsaken Remastered was not found.' 'Yellow'
    Say '  You do not need it: Forsaken VR plays on the free ForsakenX data. If you'
    Say '  own it, its soundtrack and intro film are used. Its folder holds'
    Say '  ForsakenEX.kpf; Steam puts it in ...\steamapps\common\Forsaken Remastered.'
    while ($true) {
        Say ''
        $line = Read-Host '  Drag the Forsaken Remastered folder onto this window and press Enter (or just Enter to go on without it)'
        if ($null -eq $line) { return $null }
        $line = $line.Trim().Trim('"')
        if (-not $line) { return $null }
        if (-not (Test-Path -LiteralPath $line -PathType Container)) { Say '  That is not a folder.  Try again, or press Enter to go on.'; continue }
        Add-Root $line
        $found = Find-Remaster
        if ($found) { return $found }
        Say '  No ForsakenEX.kpf in that folder.'
    }
}

# Every VR folder of ours this PC has, from their install manifests.
function Installed-VRFolders {
    $list = New-Object System.Collections.Generic.List[string]
    $add = {
        param($p)
        if (-not $p) { return }
        foreach ($x in $list) { if ($x -ieq $p) { return } }
        if (Test-Path -LiteralPath (Join-Path $p 'install-manifest.txt')) { $list.Add($p) }
    }
    if ((Split-Path -Leaf $Here) -ieq 'tools') { & $add (Split-Path -Parent $Here) }
    $rm = Find-Remaster
    if ($rm) { & $add (Join-Path $rm 'VR') }
    & $add (Join-Path (VR-Home) $ProductFile)
    & $add (Join-Path (Documents) $ProductFile)   # where an older Setup put it
    return $list
}

# --- the Steam library ---------------------------------------------------------------
#
# Steam keeps the games added with "Add a Non-Steam Game" in a binary file,
# userdata\<account>\config\shortcuts.vdf, one per Steam account on this PC.
# It reads it when it starts and writes it when it exits, so it must not be
# running while this one is changed. Every other entry is kept byte for byte.

function Vdf-ReadString([byte[]] $b, [ref] $i) {
    $start = $i.Value
    while ($b[$i.Value] -ne 0) { $i.Value++ }
    $s = [System.Text.Encoding]::UTF8.GetString($b, $start, $i.Value - $start)
    $i.Value++
    return $s
}
function Vdf-ReadMap([byte[]] $b, [ref] $i) {
    $entries = New-Object System.Collections.ArrayList
    while ($true) {
        if ($i.Value -ge $b.Length) { throw 'shortcuts.vdf ends early' }
        $t = $b[$i.Value]; $i.Value++
        if ($t -eq 8) { return ,$entries }
        $k = Vdf-ReadString $b $i
        switch ($t) {
            0 { $v = Vdf-ReadMap $b $i }
            1 { $v = Vdf-ReadString $b $i }
            2 { $v = [BitConverter]::ToInt32($b, $i.Value); $i.Value += 4 }
            7 { $v = [BitConverter]::ToUInt64($b, $i.Value); $i.Value += 8 }
            default { throw ('shortcuts.vdf has a value type this does not know (' + $t + '); left alone') }
        }
        [void]$entries.Add(@{ T = [int]$t; K = $k; V = $v })
    }
}
function Vdf-Write($entries, [System.IO.MemoryStream] $ms) {
    foreach ($e in $entries) {
        $ms.WriteByte([byte]$e.T)
        $kb = [System.Text.Encoding]::UTF8.GetBytes($e.K); $ms.Write($kb, 0, $kb.Length); $ms.WriteByte(0)
        switch ($e.T) {
            0 { Vdf-Write $e.V $ms; $ms.WriteByte(8) }
            1 { $vb = [System.Text.Encoding]::UTF8.GetBytes([string]$e.V); $ms.Write($vb, 0, $vb.Length); $ms.WriteByte(0) }
            2 { $vb = [BitConverter]::GetBytes([int32]$e.V); $ms.Write($vb, 0, 4) }
            7 { $vb = [BitConverter]::GetBytes([uint64]$e.V); $ms.Write($vb, 0, 8) }
        }
    }
}
function Vdf-Get($entries, [string] $k) {
    foreach ($e in $entries) { if ($e.K -ieq $k) { return ,$e.V } }
    return $null
}

# Steam's id for a non-Steam game: CRC-32 of the quoted exe and the name, top
# bit set. It names the library art files. (PowerShell 5.1 reads 0xFFFFFFFF
# and friends as negative int32s: the constants are written out.)
$Crc = $null
$Poly = [uint32]3988292384	# 0xEDB88320
$TopBit = [uint32]2147483648	# 0x80000000
function Crc32([string] $s) {
    if (-not $script:Crc) {
        $script:Crc = New-Object 'uint32[]' 256
        for ($n = 0; $n -lt 256; $n++) {
            $c = [uint32]$n
            for ($k = 0; $k -lt 8; $k++) { if ($c -band 1) { $c = [uint32]($Poly -bxor ($c -shr 1)) } else { $c = [uint32]($c -shr 1) } }
            $script:Crc[$n] = $c
        }
    }
    $c = [uint32]::MaxValue
    foreach ($byte in [System.Text.Encoding]::UTF8.GetBytes($s)) { $c = [uint32]($script:Crc[($c -bxor $byte) -band 0xFF] -bxor ($c -shr 8)) }
    return [uint32]($c -bxor [uint32]::MaxValue)
}
function Shortcut-Id([string] $exeQuoted, [string] $name) { return [uint32]((Crc32 ($exeQuoted + $name)) -bor $TopBit) }

function Steam-Configs {
    $s = Steam-Dir
    if (-not $s) { return @() }
    $u = Join-Path $s 'userdata'
    if (-not (Test-Path -LiteralPath $u)) { return @() }
    return @(Get-ChildItem -LiteralPath $u -Directory | Where-Object { $_.Name -match '^\d+$' -and $_.Name -ne '0' } |
        ForEach-Object { Join-Path $_.FullName 'config' } | Where-Object { Test-Path -LiteralPath $_ })
}

function Steam-Running {
    if ($SteamRoot) { return $false }	# a stand-in folder in a test
    return [bool](Get-Process -Name 'steam' -ErrorAction SilentlyContinue)
}

$SteamWasClosed = $false
function Steam-Close {
    # Steam's own way out, so it writes its files before it goes.
    $exe = Join-Path (Steam-Dir) 'steam.exe'
    Log '  Closing Steam...'
    Start-Process -FilePath $exe -ArgumentList '-shutdown'
    for ($n = 0; $n -lt 60 -and (Steam-Running); $n++) { Start-Sleep -Milliseconds 500 }
    if (Steam-Running) { Log '  Steam did not close.'; return $false }
    $script:SteamWasClosed = $true
    return $true
}
function Steam-Reopen {
    if (-not $SteamWasClosed) { return }
    Start-Process -FilePath (Join-Path (Steam-Dir) 'steam.exe')
    Log '  Steam started again.'
}

# Our entry: named as ours AND starting the game in a VR folder of ours.
function Is-Ours($entry) {
    $name = Vdf-Get $entry.V 'AppName'
    $exe = Vdf-Get $entry.V 'Exe'
    return ($name -eq $Title -and ($exe -like ('*\game\' + $GameExe + '*')))
}

function Read-Shortcuts([string] $file) {
    if (-not (Test-Path -LiteralPath $file)) {
        $root = New-Object System.Collections.ArrayList
        [void]$root.Add(@{ T = 0; K = 'shortcuts'; V = (New-Object System.Collections.ArrayList) })
        return ,$root
    }
    $b = [System.IO.File]::ReadAllBytes($file)
    $i = 0
    $root = Vdf-ReadMap $b ([ref]$i)
    if (-not ($root | Where-Object { $_.K -ieq 'shortcuts' -and $_.T -eq 0 })) { throw ('no shortcuts list in ' + $file + '; left alone') }
    return ,$root
}
function Write-Shortcuts([string] $file, $root) {
    $ms = New-Object System.IO.MemoryStream
    Vdf-Write $root $ms
    $ms.WriteByte(8)
    $backup = $file + '.before-forsaken-vr'
    if ((Test-Path -LiteralPath $file) -and -not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $file -Destination $backup }
    [System.IO.File]::WriteAllBytes($file, $ms.ToArray())
}
function Renumber($list) {
    for ($n = 0; $n -lt $list.Count; $n++) { $list[$n].K = [string]$n }
}

function New-ShortcutEntry([string] $vr) {
    # The game itself, not a launcher: no console window, and Steam sees it
    # running (playtime, overlay).
    $exe = '"' + (Join-Path $vr ('game\' + $GameExe)) + '"'
    $id = Shortcut-Id $exe $Title
    $v = New-Object System.Collections.ArrayList
    foreach ($f in @(
        @{ T = 2; K = 'appid'; V = [BitConverter]::ToInt32([BitConverter]::GetBytes($id), 0) },
        @{ T = 1; K = 'AppName'; V = $Title },
        @{ T = 1; K = 'Exe'; V = $exe },
        @{ T = 1; K = 'StartDir'; V = ('"' + (Join-Path $vr 'game') + '\"') },
        @{ T = 1; K = 'icon'; V = (Join-Path $vr ('game\' + $GameExe)) },
        @{ T = 1; K = 'ShortcutPath'; V = '' },
        @{ T = 1; K = 'LaunchOptions'; V = $VRArgs },
        @{ T = 2; K = 'IsHidden'; V = 0 },
        @{ T = 2; K = 'AllowDesktopConfig'; V = 1 },
        @{ T = 2; K = 'AllowOverlay'; V = 1 },
        @{ T = 2; K = 'OpenVR'; V = 1 },	# a VR game: listed in SteamVR's library too
        @{ T = 2; K = 'Devkit'; V = 0 },
        @{ T = 1; K = 'DevkitGameID'; V = '' },
        @{ T = 2; K = 'DevkitOverrideAppID'; V = 0 },
        @{ T = 2; K = 'LastPlayTime'; V = 0 },
        @{ T = 1; K = 'FlatpakAppID'; V = '' },
        @{ T = 0; K = 'tags'; V = (New-Object System.Collections.ArrayList) })) { [void]$v.Add($f) }
    return @{ Entry = @{ T = 0; K = '0'; V = $v }; Id = $id }
}

function Steam-Art([string] $config, [uint32] $id, [bool] $add, [string] $vr) {
    $grid = Join-Path $config 'grid'
    $files = @(($id.ToString() + '_hero.png'), ($id.ToString() + '.png'))
    if ($add) {
        $splash = Join-Path $vr 'game\vrsplash.png'
        if (-not (Test-Path -LiteralPath $splash)) { return }
        New-Item -ItemType Directory -Force -Path $grid | Out-Null
        foreach ($f in $files) { Copy-Item -LiteralPath $splash -Destination (Join-Path $grid $f) -Force }
    } else {
        foreach ($f in $files) { $p = Join-Path $grid $f; if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force } }
    }
}

function Steam-Add([string] $vr) {
    $configs = Steam-Configs
    if ($configs.Count -eq 0) { return 'no Steam' }
    foreach ($c in $configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        $root = Read-Shortcuts $file
        $list = Vdf-Get $root 'shortcuts'
        foreach ($e in @($list)) { if (Is-Ours $e) { [void]$list.Remove($e) } }
        $new = New-ShortcutEntry $vr
        [void]$list.Add($new.Entry)
        Steam-Art $c $new.Id $true $vr
        Renumber $list
        Write-Shortcuts $file $root
    }
    return 'added'
}

function Steam-HasOurs {
    foreach ($c in Steam-Configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        if (-not (Test-Path -LiteralPath $file)) { continue }
        try { foreach ($e in (Vdf-Get (Read-Shortcuts $file) 'shortcuts')) { if (Is-Ours $e) { return $true } } } catch { }
    }
    return $false
}

function Steam-Remove {
    foreach ($c in Steam-Configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        if (-not (Test-Path -LiteralPath $file)) { continue }
        $root = Read-Shortcuts $file
        $list = Vdf-Get $root 'shortcuts'
        $removed = 0
        foreach ($e in @($list)) {
            if (Is-Ours $e) {
                Steam-Art $c ([uint32][BitConverter]::ToUInt32([BitConverter]::GetBytes([int32](Vdf-Get $e.V 'appid')), 0)) $false ''
                [void]$list.Remove($e); $removed++
            }
        }
        if ($removed) { Renumber $list; Write-Shortcuts $file $root }
    }
}

function Steam-Ready([bool] $quietAllowed) {
    if (-not (Steam-Running)) { return $true }
    if ($NoPrompt) {
        if ($quietAllowed -and $CloseSteam) { return (Steam-Close) }
        return $false
    }
    if (Ask-YesNo 'Steam is open, and it has to be closed for this.  Close Steam now (it opens again after)?') { return (Steam-Close) }
    return $false
}

# --- the game data ---------------------------------------------------------------

function Install-Data([string] $game) {
    $dst = Join-Path $game 'Data'
    if (Test-Path -LiteralPath (Join-Path $dst 'levels')) { Log '  Game data: already there, kept.'; return $true }

    # A copy the player already has always beats a download.
    if (-not $DataPath) {
        foreach ($guess in @((Join-Path (Split-Path -Parent $Here) 'forsaken-data'), (Join-Path (Split-Path -Parent $Here) 'data'))) {
            if (Test-Path -LiteralPath (Join-Path $guess 'levels')) { $script:DataPath = (Resolve-Path -LiteralPath $guess).ProviderPath; break }
        }
    }
    if ($DataPath -and (Test-Path -LiteralPath (Join-Path $DataPath 'levels'))) {
        Log ('  Game data: copying from ' + $DataPath + ' (about 175 MB)')
        robocopy $DataPath $dst /e /njh /njs /ndl /nc /ns /np | Out-Null
    }
    elseif ($NoDownload) {
        Log ('  Game data: not found, and -NoDownload was given.  Get it from ' + $DATA_REPO)
        return $false
    }
    else {
        Log ('  Game data: downloading the ForsakenX data set from ' + $DATA_REPO + ' (about 130 MB)')
        $tmpZip = Join-Path $env:TEMP ('forsaken-data-' + [Guid]::NewGuid().ToString('N') + '.zip')
        $tmpDir = Join-Path $env:TEMP ('forsaken-data-' + [Guid]::NewGuid().ToString('N'))
        try {
            # Some older systems still default to TLS 1.0, which GitHub refuses.
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $DATA_URL -OutFile $tmpZip -UseBasicParsing -TimeoutSec 900
            Log ('  downloaded {0:N1} MB' -f ((Get-Item -LiteralPath $tmpZip).Length / 1MB))
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [System.IO.Compression.ZipFile]::ExtractToDirectory($tmpZip, $tmpDir)
            # GitHub archives nest everything under <repo>-<branch>/.
            $inner = Get-ChildItem -LiteralPath $tmpDir -Directory | Select-Object -First 1
            $root = $tmpDir
            if ($inner -and (Test-Path -LiteralPath (Join-Path $inner.FullName 'levels'))) { $root = $inner.FullName }
            if (-not (Test-Path -LiteralPath (Join-Path $root 'levels'))) { throw 'the download held no levels folder; the data set''s layout may have changed' }
            robocopy $root $dst /e /njh /njs /ndl /nc /ns /np | Out-Null
        } catch {
            Log ('  The download failed: ' + $_.Exception.Message)
            Log ('  Download it yourself from ' + $DATA_REPO + ' (Code, then Download ZIP), unzip it,')
            Log '  and drag its folder onto Setup.bat.'
            return $false
        } finally {
            foreach ($t in @($tmpZip, $tmpDir)) { if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue } }
        }
    }
    if (Test-Path -LiteralPath (Join-Path $dst 'levels')) { Log '  Game data: installed.'; return $true }
    Log '  Game data: the copy did not complete.'
    return $false
}

function Copy-RemasterMedia([string] $game, [string] $rm) {
    if (-not $rm) { Log '  Soundtrack and intro film: Forsaken Remastered not found, so none (both are optional).'; return }
    $src = Join-Path $rm 'music\OGG'
    $n = 0
    if (Test-Path -LiteralPath $src) {
        $dst = Join-Path $game 'Music\OGG'
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        foreach ($f in Get-ChildItem -LiteralPath $src -File -Filter *.ogg) { Copy-Item -LiteralPath $f.FullName -Destination $dst -Force; $n++ }
    }
    $intro = Join-Path $rm 'movies\intro.ogv'
    $film = $false
    if (Test-Path -LiteralPath $intro) {
        New-Item -ItemType Directory -Force -Path (Join-Path $game 'Movies') | Out-Null
        Copy-Item -LiteralPath $intro -Destination (Join-Path $game 'Movies') -Force
        $film = $true
    }
    Log ('  From your Forsaken Remastered: ' + $n + ' music tracks' + $(if ($film) { ' and the intro film.' } else { ', no intro film.' }))
}

function Install-OriginalTextures([string] $game, [string] $rm) {
    $kpf = $(if ($rm) { Join-Path $rm 'ForsakenEX.kpf' } else { '' })
    if (-not $kpf -or -not (Test-Path -LiteralPath $kpf)) { Log '  1998 art: needs Forsaken Remastered, which was not found; skipped.'; return }
    $dstOrig = Join-Path $game 'Data\textures\original'
    New-Item -ItemType Directory -Force -Path $dstOrig | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Add-Type -AssemblyName System.Drawing
    $zip = [System.IO.Compression.ZipFile]::OpenRead($kpf)
    $n = 0
    try {
        foreach ($e in $zip.Entries) {
            if ($e.FullName -notlike 'textures/*' -or [IO.Path]::GetExtension($e.Name).ToLower() -ne '.bmp') { continue }
            $tmpBmp = Join-Path $env:TEMP ('fsx_' + $e.Name)
            try {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $tmpBmp, $true)
                $img = [System.Drawing.Image]::FromFile($tmpBmp)
                $img.Save((Join-Path $dstOrig ([IO.Path]::ChangeExtension($e.Name, '.png').ToLower())), [System.Drawing.Imaging.ImageFormat]::Png)
                $img.Dispose(); $n++
            } catch { }
            if (Test-Path -LiteralPath $tmpBmp) { Remove-Item -LiteralPath $tmpBmp -Force -ErrorAction SilentlyContinue }
        }
    } finally { $zip.Dispose() }
    Log ('  1998 art: ' + $n + ' textures.  Set UseOriginalTextures = true in the settings to use them.')
}

function Install-BonusLevels([string] $game, [string] $rm) {
    # The Remaster's extra levels (the N64 ones too), same file format, into
    # the MULTIPLAYER list (battle.dat): the campaign's own list drives its
    # ending. None carry Capture The Flag goals; some start at the origin.
    $kpf = $(if ($rm) { Join-Path $rm 'ForsakenEX.kpf' } else { '' })
    if (-not $kpf -or -not (Test-Path -LiteralPath $kpf)) { Log '  Bonus levels: need Forsaken Remastered, which was not found; skipped.'; return }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Add-Type -AssemblyName System.Drawing
    $levelsDir = Join-Path $game 'Data\levels'
    $zip = [System.IO.Compression.ZipFile]::OpenRead($kpf)
    $added = @()
    try {
        $candidates = @()
        foreach ($e in $zip.Entries) { if ($e.FullName -match '^levels/([^/]+)/\1\.mxv$') { $candidates += $Matches[1] } }
        foreach ($lv in ($candidates | Sort-Object -Unique)) {
            if (Test-Path -LiteralPath (Join-Path $levelsDir (Join-Path $lv ($lv + '.mxv')))) { continue }
            foreach ($e in $zip.Entries) {
                if (-not $e.FullName.StartsWith('levels/' + $lv + '/') -or $e.Name -eq '') { continue }
                $parts = $e.FullName.Substring(7).Split('/')
                if ($parts.Count -gt 2 -and @('de', 'es', 'fr', 'it') -contains $parts[1]) { continue }
                $dest = [System.IO.Path]::Combine($levelsDir, ($parts -join '\'))
                [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($dest)) | Out-Null
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
                if ([IO.Path]::GetExtension($dest).ToLower() -eq '.bmp') {
                    try { $img = [System.Drawing.Image]::FromFile($dest); $img.Save([IO.Path]::ChangeExtension($dest, '.png'), [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose() } catch { }
                }
            }
            $added += $lv
        }
    } finally { $zip.Dispose() }
    if ($added.Count) {
        $battle = Join-Path $levelsDir 'battle.dat'
        $existing = @()
        if (Test-Path -LiteralPath $battle) { $existing = @(Get-Content -LiteralPath $battle | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
        $new = @($added | Where-Object { $existing -notcontains $_ })
        $room = 64 - $existing.Count	# MAXLEVELS
        if ($new.Count -gt $room) { $new = $new[0..([Math]::Max($room - 1, 0))] }
        if ($new.Count) { Add-Content -LiteralPath $battle -Value $new -Encoding ASCII }
        Log ('  Bonus levels: ' + $added.Count + ' added to the multiplayer list.')
    } else { Log '  Bonus levels: none the data set does not already have.' }
}

# --- install ---------------------------------------------------------------------

function Write-Launcher([string] $path, [string] $title, [string] $gameArgs) {
    $lines = @(
        '@echo off',
        ('rem ' + $title),
        ('rem Made by ' + $ProductFile + ' ' + $Version + ' Setup.'),
        'rem No parenthesised blocks below: a path with "(x86)" in it ends them early.',
        'setlocal',
        ('if not exist "%~dp0game\' + $GameExe + '" goto noexe'),
        'if not exist "%~dp0game\Data\levels" goto nodata',
        'rem pushd, not cd /d: it works from a network share too.  .\ because a',
        'rem program named alone can be refused (NoDefaultCurrentDirectoryInExePath).',
        'rem start: the game runs on its own and this window closes at once.',
        'pushd "%~dp0game"',
        (('start "" ".\' + $GameExe + '" ' + $gameArgs).TrimEnd() + ' %*'),
        'popd',
        'goto :eof',
        '',
        ':nodata',
        'echo The game data is missing from "%~dp0game\Data".',
        ('echo Run Setup.bat again from the ' + $ProductFile + ' download.'),
        'pause',
        'exit /b 1',
        '',
        ':noexe',
        ('echo ' + $GameExe + ' is missing from "%~dp0game".  Run Setup.bat again.'),
        'pause',
        'exit /b 1'
    )
    Set-Content -LiteralPath $path -Value $lines -Encoding ASCII
}

function Make-Shortcut([string] $lnk, [string] $target, [string] $arguments, [string] $workdir, [string] $what) {
    $sh = New-Object -ComObject WScript.Shell
    $s = $sh.CreateShortcut($lnk)
    $s.TargetPath = $target
    $s.Arguments = $arguments
    $s.WorkingDirectory = $workdir
    $s.IconLocation = $target + ',0'
    $s.Description = $what
    $s.WindowStyle = 1
    $s.Save()
}

function VR-Folder-For([string] $rm) {
    $candidates = @()
    if ($rm) { $candidates += (Join-Path $rm 'VR') }
    $candidates += (Join-Path (VR-Home) $ProductFile)
    foreach ($vr in $candidates) {
        try {
            New-Item -ItemType Directory -Force -Path (Join-Path $vr 'game') | Out-Null
            $probe = Join-Path $vr 'write-test.tmp'
            Set-Content -LiteralPath $probe -Value 'x'
            Remove-Item -LiteralPath $probe -Force
            return $vr
        } catch {
            # Under Program Files a game folder is not writable without admin:
            # the VR folder goes in Documents (or Games) instead.
            Log ('  ' + $vr + ' cannot be written to; trying ' + (VR-Home) + '.')
        }
    }
    throw 'no folder could be written to for the VR install'
}

function Do-Install {
    Banner 'Setup'
    if (-not $Package) { throw 'Run Setup.bat from the folder you unzipped (it holds files\game).' }
    Search-Roots
    Log ('Looking in ' + $Roots.Count + ' place(s) for Forsaken Remastered.')
    foreach ($r in $Roots) { Add-Content -LiteralPath $LogFile -Value ('  ' + $r) -Encoding UTF8 }
    $rm = Find-Remaster
    if (-not $rm) { $rm = Ask-Folder }
    if ($rm) { Log ('Forsaken Remastered: ' + $rm) } else { Log 'Forsaken Remastered: not found.  Forsaken VR does not need it.' }
    $Result['remaster'] = $rm

    $vr = VR-Folder-For $rm
    $game = Join-Path $vr 'game'
    Log ('Installing to ' + $vr)
    $note = Join-Path $vr 'Your saves are kept here.txt'
    if (Test-Path -LiteralPath $note) { Remove-Item -LiteralPath $note -Force }

    $manifest = New-Object System.Collections.Generic.List[string]
    # The game: replaced on every run (an update), except the player's own.
    $src = Join-Path $Package 'game'
    foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) {
        $rel = $f.FullName.Substring($src.Length + 1)
        $dst = Join-Path $game $rel
        $isPlayer = $false
        foreach ($p in $PlayerFiles) { if ($rel -ieq $p) { $isPlayer = $true } }
        if ($isPlayer -and (Test-Path -LiteralPath $dst)) { continue }	# never overwrite settings
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        if (-not $isPlayer) { $manifest.Add('game\' + $rel) }
    }
    foreach ($d in $PlayerDirs + @('logs')) { New-Item -ItemType Directory -Force -Path (Join-Path $game $d) | Out-Null }
    # The splash (Game Or Die art), if this download carries one.
    $splash = Join-Path $Here 'vrsplash.png'
    if (Test-Path -LiteralPath $splash) {
        Copy-Item -LiteralPath $splash -Destination (Join-Path $game 'vrsplash.png') -Force
        $manifest.Add('game\vrsplash.png')
    }

    # The data, the music and the film. Folders Setup fills are listed whole.
    $dataOk = Install-Data $game
    $manifest.Add('dir:game\Data')
    Copy-RemasterMedia $game $rm
    $manifest.Add('dir:game\Music\OGG')
    $manifest.Add('dir:game\Movies')
    if ($OriginalTextures) { Install-OriginalTextures $game $rm }
    if ($BonusLevels) { Install-BonusLevels $game $rm }

    Write-Launcher (Join-Path $vr $Launcher) 'Forsaken in VR.' $VRArgs
    Write-Launcher (Join-Path $vr $Flat) 'Forsaken on the monitor.' '-window -log'
    $manifest.Add($Launcher)
    $manifest.Add($Flat)

    # The kit, so Uninstall and Collect logs are there without the download.
    New-Item -ItemType Directory -Force -Path (Join-Path $vr 'tools') | Out-Null
    Copy-Item -LiteralPath (Join-Path $Here 'setup.ps1') -Destination (Join-Path $vr 'tools\setup.ps1') -Force
    $manifest.Add('tools\setup.ps1')
    foreach ($k in @('Uninstall.bat', 'Collect logs.bat', 'README.md', 'LICENSE')) {
        $s = Join-Path $Here $k
        if (-not (Test-Path -LiteralPath $s)) { $s = Join-Path (Split-Path -Parent $Here) $k }
        if (Test-Path -LiteralPath $s) {
            Copy-Item -LiteralPath $s -Destination (Join-Path $vr $k) -Force
            $manifest.Add($k)
        }
    }
    Set-Content -LiteralPath (Join-Path $vr 'version.txt') -Value ($Product + ' ' + $Version) -Encoding ASCII
    $manifest.Add('version.txt')

    if (-not $NoShortcuts) {
        Make-Shortcut (Join-Path (Desktop) $ShortcutName) (Join-Path $game $GameExe) $VRArgs $game 'Forsaken in VR'
        Log ('Desktop shortcut: ' + $ShortcutName)
    }

    $manifest.Add('setup-log.txt')
    $manifest.Add('install-manifest.txt')
    Set-Content -LiteralPath (Join-Path $vr 'install-manifest.txt') -Value (@(
        ('# ' + $Product + ' ' + $Version + ', installed ' + (Get-Date -Format 'yyyy-MM-dd HH:mm')),
        ('# remaster: ' + $(if ($rm) { $rm } else { 'none' })),
        '# Uninstall removes exactly these files and folders (dir:).  Your pilot, saved games',
        '# and settings are not in it.') + $manifest) -Encoding UTF8

    if (-not $dataOk) {
        Log ''
        Log 'The game is installed, but its data is not: see above, then run Setup.bat again.'
        $Result['result'] = 'no game data'
        Copy-Item -LiteralPath $LogFile -Destination (Join-Path $vr 'setup-log.txt') -Force
        Finish 1
    }

    # The Steam library, if Steam is here and the player wants it.
    $Result['steam'] = 'skipped'
    if (-not $NoSteam -and (Steam-Configs).Count -gt 0) {
        $want = $false
        if ($NoPrompt) { $want = [bool]$AddToSteam }
        else { Say ''; $want = Ask-YesNo ('Add ' + $Title + ' to your Steam library?') }
        if ($want) {
            if (Steam-Ready $true) {
                $Result['steam'] = Steam-Add $vr
                Log 'Added to your Steam library (and SteamVR''s).'
                Steam-Reopen
            } else {
                $Result['steam'] = 'skipped: Steam is open'
                Log 'Not added to Steam: Steam has to be closed.  Run Setup.bat again any time to add it.'
            }
        }
    }

    Log ''
    Log 'Done.  Start it from the desktop shortcut, or "Forsaken VR.cmd" in the VR folder.'
    Log 'Put your headset on first, with Virtual Desktop or SteamVR running.'
    Log 'Uninstall.bat and Collect logs.bat are in the VR folder too.'
    Copy-Item -LiteralPath $LogFile -Destination (Join-Path $vr 'setup-log.txt') -Force
    $Result['vrFolder'] = $vr
    $Result['result'] = 'installed'
    if (-not $NoPrompt) { try { Start-Process explorer.exe -ArgumentList ('"' + $vr + '"') } catch { } }
}

# --- uninstall -------------------------------------------------------------------

function Remove-Empty([string] $dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return }
    foreach ($d in Get-ChildItem -LiteralPath $dir -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending) {
        if (-not (Get-ChildItem -LiteralPath $d.FullName -Force)) { Remove-Item -LiteralPath $d.FullName -Force }
    }
    if (-not (Get-ChildItem -LiteralPath $dir -Force)) { Remove-Item -LiteralPath $dir -Force }
}

function Do-Uninstall {
    Banner 'Uninstall'
    Search-Roots
    $folders = Installed-VRFolders

    # The Steam library first, while the VR folder still says what is ours.
    $Result['steam'] = 'none'
    if (Steam-HasOurs) {
        if (Steam-Ready $true) {
            Steam-Remove
            $Result['steam'] = 'removed'
            Log 'Removed from your Steam library.'
            Steam-Reopen
        } else {
            $Result['steam'] = 'left: Steam is open'
            Log 'Still in your Steam library: Steam has to be closed.  Close it and run Uninstall again.'
        }
    }

    if ($folders.Count -eq 0) { Log 'Nothing else to uninstall: no VR folder was found.'; $Result['result'] = 'nothing installed'; return }
    foreach ($vr in $folders) {
        Log ('Removing ' + $vr)
        $game = Join-Path $vr 'game'
        $listed = Get-Content -LiteralPath (Join-Path $vr 'install-manifest.txt') | Where-Object { $_ -and -not $_.StartsWith('#') }
        foreach ($rel in $listed) {
            if ($rel.StartsWith('dir:')) {
                $p = Join-Path $vr $rel.Substring(4)
                if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
            } else {
                $p = Join-Path $vr $rel
                if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
            }
        }
        # What the game wrote that is not the player's: logs, crash reports.
        foreach ($pat in @('logs\*.txt', 'logs\*.ppm', 'crash.txt', 'crash.prev.txt', 'gmon.out', 'steam_appid.txt')) {
            Get-ChildItem -Path (Join-Path $game $pat) -File -ErrorAction SilentlyContinue | Remove-Item -Force
        }
        if (Test-Path -LiteralPath (Join-Path $vr 'steam_appid.txt')) { Remove-Item -LiteralPath (Join-Path $vr 'steam_appid.txt') -Force }
        # No saved game and no pilot: the settings left are only Setup's
        # first copy, so the folder goes too.
        $saves = @()
        foreach ($d in $PlayerDirs) { $saves += @(Get-ChildItem -LiteralPath (Join-Path $game $d) -File -ErrorAction SilentlyContinue) }
        if ($saves.Count -eq 0) {
            foreach ($p in $PlayerFiles) { $q = Join-Path $game $p; if (Test-Path -LiteralPath $q) { Remove-Item -LiteralPath $q -Force } }
        }
        Remove-Empty $vr
        if (Test-Path -LiteralPath $vr) {
            Set-Content -LiteralPath (Join-Path $vr 'Your saves are kept here.txt') -Value @(
                ($Product + ' was uninstalled.  Your pilot, saved games and settings are'),
                'kept in the game folder here, and come back if you install it again.',
                'Delete this VR folder to remove them too.') -Encoding ASCII
            Log '  Your pilot, saved games and settings are kept there.'
        } else { Log '  Removed.' }
    }
    $lnk = Join-Path (Desktop) $ShortcutName
    if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force; Log ('Removed the desktop shortcut ' + $ShortcutName) }
    Log ''
    Log ($Product + ' is uninstalled.  Forsaken Remastered itself is untouched.')
    $Result['result'] = 'uninstalled'
}

# --- collect logs ------------------------------------------------------------------

function Do-Collect {
    Banner 'Collect logs'
    Search-Roots
    $stamp = Get-Date -Format 'yyyyMMdd-HHmm'
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-logs-' + $stamp)
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory -Path $work | Out-Null

    $folders = Installed-VRFolders
    $n = 0
    foreach ($vr in $folders) {
        $dst = Join-Path $work ('vr-' + $n); $n++
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        foreach ($f in @('install-manifest.txt', 'setup-log.txt', 'version.txt')) {
            $p = Join-Path $vr $f
            if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $dst }
        }
        $game = Join-Path $vr 'game'
        foreach ($f in @('crash.txt', 'crash.prev.txt', 'configs\debug.txt')) {
            $p = Join-Path $game $f
            if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $dst }
        }
        # The newest five game logs.
        Get-ChildItem -LiteralPath (Join-Path $game 'logs') -Filter '*.txt' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 5 | Copy-Item -Destination $dst
        Log ('Logs from ' + $vr)
    }
    foreach ($m in @('install', 'uninstall')) {
        $p = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-' + $m + '.txt')
        if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $work }
    }

    # This PC: what a diagnosis needs, nothing more.
    $pc = @(($Product + ' ' + $Version + ' - this PC, ' + (Get-Date -Format 'yyyy-MM-dd HH:mm')), '')
    try { $os = Get-CimInstance Win32_OperatingSystem; $pc += ('Windows: ' + $os.Caption + ' ' + $os.Version) } catch { }
    try { foreach ($v in Get-CimInstance Win32_VideoController) { $pc += ('Graphics: ' + $v.Name + ', driver ' + $v.DriverVersion) } } catch { }
    foreach ($k in @('HKLM:\SOFTWARE\Khronos\OpenXR\1', 'HKCU:\SOFTWARE\Khronos\OpenXR\1')) {
        try { $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop; if ($v.ActiveRuntime) { $pc += ('OpenXR runtime (' + $k + '): ' + $v.ActiveRuntime) } } catch { }
    }
    foreach ($p in @('VirtualDesktop.Streamer', 'vrserver', 'vrcompositor', 'OVRServer_x64')) {
        if (Get-Process -Name $p -ErrorAction SilentlyContinue) { $pc += ('Running: ' + $p) }
    }
    $pc += ''
    $pc += ('VR folders found: ' + $folders.Count)
    foreach ($vr in $folders) { $pc += ('  ' + $vr) }
    $pc += ('In the Steam library: ' + (Steam-HasOurs))
    Set-Content -LiteralPath (Join-Path $work 'this-pc.txt') -Value $pc -Encoding UTF8

    $zip = Join-Path (Desktop) ('Forsaken-VR-logs-' + $stamp + '.zip')
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip
    Remove-Item -LiteralPath $work -Recurse -Force
    Log ''
    Log ('Made ' + $zip)
    Log 'Send that file to us (Discord or a GitHub issue) with a line on what happened.'
    $Result['zip'] = $zip
    if (-not $NoPrompt) { try { Start-Process explorer.exe -ArgumentList ('/select,"' + $zip + '"') } catch { } }
}

# --- status, for -Detect and -Json -------------------------------------------------

function Game-Status {
    Search-Roots
    $rm = Find-Remaster
    $vr = $null
    foreach ($f in Installed-VRFolders) { $vr = $f; break }
    $ver = $null
    if ($vr -and (Test-Path -LiteralPath (Join-Path $vr 'version.txt'))) { $ver = [string](Get-Content -LiteralPath (Join-Path $vr 'version.txt') -TotalCount 1) }
    return [pscustomobject][ordered]@{
        name = 'Forsaken'; title = $Title
        remasterFound = [bool]$rm
        remasterFolder = $rm
        installed = [bool]$vr
        vrFolder = $vr
        installedVersion = $ver
        launcher = $(if ($vr) { Join-Path $vr $Launcher } else { $null })
        flatLauncher = $(if ($vr) { Join-Path $vr $Flat } else { $null })
        dataReady = $(if ($vr) { Test-Path -LiteralPath (Join-Path $vr 'game\Data\levels') } else { $false })
    }
}

function Do-Detect {
    Banner 'Find the game'
    Search-Roots
    Log ('Looking in ' + $Roots.Count + ' place(s):')
    foreach ($r in $Roots) { Log ('  ' + $r) }
    $s = Game-Status
    Log ('Forsaken Remastered: ' + $(if ($s.remasterFound) { $s.remasterFolder } else { 'not found' }))
    Log ('Forsaken VR: ' + $(if ($s.installed) { $s.installedVersion + ' in ' + $s.vrFolder } else { 'not installed' }))
    $Result['steamLibrary'] = Steam-HasOurs
}

switch ($Mode) {
    'detect'    { Do-Detect }
    'install'   { Do-Install }
    'uninstall' { Do-Uninstall }
    'collect'   { Do-Collect }
}
Finish 0
