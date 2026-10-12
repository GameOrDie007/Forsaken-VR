# Forsaken VR for Quest and Steam Frame (standalone): install, uninstall, log
# collection and start, in one script, run on the PC with the headset plugged in
# by USB (a Quest) or reached over Wi-Fi (a Steam Frame running Lepton).
#
# It gathers the game the same way the PC Setup does, because it uses the PC
# Setup's own code (setup-common.ps1 beside it, loaded as a library): the ForsakenX data
# set, downloaded from that project's repository (or a copy you already have),
# and the soundtrack and intro film from YOUR Forsaken Remastered if you own it.
# All of that is put together in a folder on this PC, kept for the next update,
# then installed on the headset with the app.  No game data ships with this port.
#
#   Setup.bat                   install (or update) on a Quest, by USB
#   Setup Steam Frame.bat       the same on a Steam Frame, over Wi-Fi (-Serial)
#   Start on Steam Frame.bat    -Start: Lepton has no launcher for the app
#   Uninstall.bat               -Uninstall: copies your pilots, saved games and
#                               settings onto this PC, then removes the app
#   Collect logs.bat            -Collect: the headset's logs, in one zip on your desktop
#
# The headset talks to the PC through adb, Google's Android tool.  This script
# uses one already on the PC (SideQuest's, or one on PATH), or downloads
# Google's own the first time.
#
# Safe to run again at any time: files already on the headset are not copied
# again, and pilots, saved games and settings are never overwritten.
#
# For tools and hubs (the same switches in every Game Or Die port):
#   -Quiet              no questions, no pause, no Explorer window
#   -GamePath <folder>  your Forsaken Remastered folder; no searching
#   -DataPath <folder>  a ForsakenX data set already on this PC (no download)
#   -Json               print the result as JSON on stdout (nothing else)
#   -Detect             change nothing; report the game, the headset and the install
#   -Serial <id>        the headset to use when several are connected, or a
#                       headset on the network (a Steam Frame: its name or address)
# Exit codes: 0 done, 1 no game data, 2 error, 3 no headset.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch] $Uninstall,
    [switch] $Collect,
    [switch] $Start,
    [switch] $Detect,
    [switch] $Quiet,
    [switch] $Json,
    [string[]] $GamePath,
    [string] $DataPath = '',
    [switch] $NoDownload,
    [string] $Serial,
    # Tests: no questions; only the folders given; this adb; how long to wait.
    [switch] $NoPrompt,
    [switch] $NoSearch,
    [string] $Adb,
    [int] $WaitSeconds = 600,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Dropped
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$QuestHere = (Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath

# --- the PC Setup's own search and data functions ---------------------------------
# The PC Setup sits beside this script in the download as setup-common.ps1 (not
# setup.ps1: the Game Or Die installer takes a zip with a setup.ps1 for the PC
# version); in the source tree it is package\setup.ps1.  It stops after defining
# its functions when FORSAKEN_VR_SETUP_LIBRARY is set.  The switches are handed
# on, because loading it binds its parameters afresh.
$pcSetup = Join-Path $QuestHere 'setup-common.ps1'
if (-not (Test-Path -LiteralPath $pcSetup)) { $pcSetup = Join-Path (Split-Path -Parent $QuestHere) 'setup.ps1' }
if (-not (Test-Path -LiteralPath $pcSetup)) { throw ('setup-common.ps1 is missing beside ' + $PSCommandPath) }
$keepDropped = @($Dropped); $keepGamePath = @($GamePath)
$env:FORSAKEN_VR_SETUP_LIBRARY = '1'
. $pcSetup -Quiet:$Quiet -Json:$Json -NoPrompt:$NoPrompt -NoSearch:$NoSearch -DataPath $DataPath -NoDownload:$NoDownload
$env:FORSAKEN_VR_SETUP_LIBRARY = $null
$Dropped = $keepDropped
if ($keepGamePath -and $keepGamePath[0]) { $NoSearch = $true; $Dropped = @($Dropped) + @($keepGamePath | ForEach-Object { $_ -split ';' } | Where-Object { $_ }) }
if ($Quiet -or $Json) { $NoPrompt = $true }

$Product = 'Forsaken VR'
$ProductFile = 'Forsaken VR'
$Version = '1.1'
$AppId = 'net.gameordie.forsakenvr'
$AppName = 'Forsaken VR'
$Activity = $AppId + '/.ForsakenActivity'
$Remote = '/sdcard/Android/data/' + $AppId + '/files'
$Apk = Join-Path $QuestHere 'ForsakenVR.apk'
# The game put together on this PC for the headset; kept, so an update copies only what changed.
$Stage = Join-Path $env:LOCALAPPDATA 'Game Or Die\Forsaken VR for Quest'
# What the game writes on the headset: made by the app itself (SetupReceiver),
# so the game can rewrite them.  Lowercase: the game lowercases every path off Windows.
$WritableDirs = @('configs', 'pilots', 'savegame', 'logs')
# The player's own, on the headset: kept on this PC by every Setup and Uninstall.
$SaveItems = @('configs/debug.txt', 'player.txt', 'pilots', 'savegame')
$SaveRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ($ProductFile + '\Quest saves')
$IsFrame = $false

# --- the log ----------------------------------------------------------------------

$Mode = 'install'
if ($Uninstall) { $Mode = 'uninstall' }
if ($Collect)   { $Mode = 'collect' }
if ($Detect)    { $Mode = 'detect' }
if ($Start)     { $Mode = 'start' }
$LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-quest-' + $Mode + '.txt')
if ($Mode -eq 'install' -and (Test-Path -LiteralPath $Apk)) { $LogFile = Join-Path (Split-Path -Parent $QuestHere) 'setup-log.txt' }
$Result = [ordered]@{ product = $Product; platform = 'quest'; version = $Version; mode = $Mode; result = 'ok' }
try { Set-Content -LiteralPath $LogFile -Value ($Product + ' for Quest ' + $Version + ' ' + $Mode + ', ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' on ' + $env:COMPUTERNAME) -Encoding UTF8 }
catch { $LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-quest-' + $Mode + '.txt'); Set-Content -LiteralPath $LogFile -Value ($Product + ' for Quest ' + $Mode) -Encoding UTF8 }

function Finish([int] $code) {
    if ($Json) {
        $Result['exitCode'] = $code
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
    Say ('  ' + $Product + ' for Quest ' + $Version + ': ' + $what) 'Cyan'
    Say ('  ' + ('-' * ($Product.Length + $Version.Length + $what.Length + 13)))
    Say ''
}

function Size-Text([long] $bytes) {
    if ($bytes -ge 1GB) { return ('{0:N1} GB' -f ($bytes / 1GB)) }
    return ('{0:N0} MB' -f [Math]::Max(1, [Math]::Round($bytes / 1MB)))
}

# --- the game for the headset, put together on this PC -------------------------------

# The headset's copy is all lowercase: the game lowercases every path it opens
# off Windows, and the data set has a few names with capitals in them.
function Stage-Game([string] $rm) {
    $pc = Join-Path $Stage 'pc'
    New-Item -ItemType Directory -Force -Path $pc | Out-Null
    $dataOk = Install-Data $pc
    if ($dataOk) { Copy-RemasterMedia $pc $rm }
    # The port's own files from this download: the scripts, the settings
    # template, the playlist; not the PC program and its libraries.
    $src = Join-Path $QuestHere 'game'
    if (Test-Path -LiteralPath $src) {
        foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) {
            if ($f.Extension -in @('.exe', '.dll', '.cmd', '.bat')) { continue }
            $rel = $f.FullName.Substring($src.Length + 1)
            $dst = Join-Path $pc $rel
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        }
    }
    $splash = Join-Path $QuestHere 'vrsplash.png'
    if (Test-Path -LiteralPath $splash) { Copy-Item -LiteralPath $splash -Destination (Join-Path $pc 'vrsplash.png') -Force }
    # The list of what goes to the headset: lowercase name -> file and size.
    $list = @{}
    foreach ($f in Get-ChildItem -LiteralPath $pc -Recurse -File) {
        $rel = $f.FullName.Substring($pc.Length + 1).Replace('\', '/').ToLowerInvariant()
        if ($rel -eq 'configs/debug.txt') { continue }	# the player's settings: put there once, below
        $list[$rel] = $f
    }
    return @{ Ok = $dataOk; Files = $list; Pc = $pc }
}

# --- adb: talking to the headset ----------------------------------------------------------

$AdbExe = $null
$AdbCache = Join-Path $env:LOCALAPPDATA 'Game Or Die\platform-tools'

function Find-Adb {
    if ($Adb) { return $Adb }
    $c = @()
    try { $c += (Get-Command adb.exe -ErrorAction Stop).Source } catch { }
    $c += Join-Path $env:APPDATA 'SideQuest\platform-tools\adb.exe'
    $c += Join-Path $env:LOCALAPPDATA 'Programs\SideQuest\resources\app.asar.unpacked\build\platform-tools\adb.exe'
    $c += Join-Path $AdbCache 'adb.exe'
    foreach ($p in $c) { if ($p -and (Test-Path -LiteralPath $p)) { return $p } }
    return $null
}

function Get-Adb {
    $script:AdbExe = Find-Adb
    if ($AdbExe) { Log ('adb: ' + $AdbExe); return }
    Say '  To talk to the headset this needs adb, Google''s Android tool (about 7 MB).'
    Say '  It comes straight from Google, under their terms:'
    Say '  https://developer.android.com/studio/releases/platform-tools'
    if (-not $NoPrompt -and -not (Ask-YesNo 'Download it now?')) { throw 'adb is needed; nothing was changed.' }
    $zip = Join-Path ([System.IO.Path]::GetTempPath()) 'gameordie-platform-tools.zip'
    Log 'Downloading Google''s platform tools...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip' -OutFile $zip
    $parent = Split-Path -Parent $AdbCache
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    if (Test-Path -LiteralPath $AdbCache) { Remove-Item -LiteralPath $AdbCache -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $parent -Force
    Remove-Item -LiteralPath $zip -Force
    $script:AdbExe = Join-Path $AdbCache 'adb.exe'
    if (-not (Test-Path -LiteralPath $AdbExe)) { throw 'the platform tools download had no adb.exe' }
    Log ('adb: ' + $AdbExe + ' (downloaded)')
}

# Runs adb and returns @{ Code; Out } (stdout and stderr together).  Through a
# Process, so adb's progress on stderr never trips PowerShell's error handling.
function Run-Adb([string[]] $a, [switch] $NoSerial) {
    $all = @()
    if ($script:Serial -and -not $NoSerial) { $all += @('-s', $script:Serial) }
    $all += $a
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $AdbExe
    $psi.Arguments = (($all | ForEach-Object { if ($_ -match '[\s"&]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync()
    $e = $p.StandardError.ReadToEndAsync()
    $p.WaitForExit()
    # If this call started adb's server, the server holds our output open and
    # it never ends: once adb itself is done, wait a moment, not forever.
    $done = $o.Wait(5000) -and $e.Wait(1000)
    $out = if ($done) { ($o.Result + $e.Result).Trim() } else { '' }
    return @{ Code = $p.ExitCode; Out = $out }
}

# adb's server, started on its own through the shell so that it inherits no
# handle of ours (a hub reading -Json may be capturing this script's output).
function Start-AdbServer {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $AdbExe
    $psi.Arguments = 'start-server'
    $psi.UseShellExecute = $true
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $p = [System.Diagnostics.Process]::Start($psi)
    [void]$p.WaitForExit(30000)
}

function Shell([string] $cmd) { return (Run-Adb @('shell', $cmd)).Out }

# Waits for one headset that has allowed this PC, saying what to do meanwhile.
function Wait-Headset {
    Start-AdbServer
    # A headset on the network (a name or an address, not a USB serial): connect to it.
    if ($Serial -and $Serial -notmatch '^emulator-' -and $Serial -match '[.:]|^[A-Za-z][\w-]*$' -and $Serial -notmatch '^[0-9A-Z]{10,}$') {
        if ($Serial -notmatch ':\d+$') { $script:Serial = $Serial + ':5555' }
        $r = Run-Adb @('connect', $Serial) -NoSerial
        Log ('network headset ' + $Serial + ': ' + $r.Out)
    }
    $said = ''
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ($true) {
        $lines = ((Run-Adb @('devices', '-l') -NoSerial).Out -split "`r?`n") | Where-Object { $_ -match '^\S+\s+(device|unauthorized|offline|authorizing|no permissions)' }
        $devs = @($lines | ForEach-Object { $f = $_ -split '\s+'; @{ Id = $f[0]; State = $f[1]; Line = $_ } })
        # No headset named: the Quest on the cable, never one left connected over the
        # network (a Steam Frame is reached only with -Serial).
        if ($Serial) { $devs = @($devs | Where-Object { $_.Id -eq $Serial }) }
        else { $devs = @($devs | Where-Object { $_.Id -notmatch ':\d+$' }) }
        $ready = @($devs | Where-Object { $_.State -eq 'device' })
        if ($ready.Count -ge 1) {
            # Several: the Quest among them, else the first.
            $pick = $ready | Where-Object { $_.Line -match 'Quest|eureka|hollywood|seacliff|panther' } | Select-Object -First 1
            if (-not $pick) { $pick = $ready[0] }
            $script:Serial = $pick.Id
            return $true
        }
        $now = 'none'
        if ($devs | Where-Object { $_.State -eq 'unauthorized' -or $_.State -eq 'authorizing' }) { $now = 'unauthorized' }
        elseif ($devs | Where-Object { $_.State -eq 'offline' -or $_.State -eq 'no permissions' }) { $now = 'offline' }
        if ($now -ne $said) {
            $said = $now
            Say ''
            switch ($now) {
                'none' {
                    Say '  Waiting for the headset.' 'Yellow'
                    Say '  Turn it on and plug it into this PC with a USB-C cable (the one it charges'
                    Say '  with works if it carries data).  Developer mode has to be on: README.txt,'
                    Say '  "Before you start", shows how in a minute.'
                }
                'unauthorized' {
                    Say '  The headset is asking whether to trust this PC.' 'Yellow'
                    Say '  Put it on and choose Allow (tick "Always allow from this computer").'
                    Say '  No message?  Unplug the cable and plug it back in, with the headset on.'
                }
                'offline' {
                    Say '  The headset is connected but not answering.' 'Yellow'
                    Say '  Unplug the cable and plug it back in.'
                }
            }
            Log ('headset: ' + $now)
        }
        if ((Get-Date) -gt $deadline) { return $false }
        Start-Sleep -Seconds 2
    }
}

function Headset-Info {
    $make = Shell 'getprop ro.product.manufacturer'
    $model = Shell 'getprop ro.product.model'
    $os = Shell 'getprop ro.build.version.release'
    Log ('headset: ' + $Serial + ', ' + $make + ' ' + $model + ', Android ' + $os)
    $Result['headset'] = [ordered]@{ id = $Serial; make = $make; model = $model; android = $os }
    if ($make -match 'Valve' -or $model -match 'Lepton') {
        # A Steam Frame (the app runs under Lepton): its pilots get their own folder.
        $script:IsFrame = $true
        $script:SaveRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ($ProductFile + '\Steam Frame saves')
        Say '  This is a Steam Frame: the same app runs there, under Lepton.'
    } elseif (-not ($make -match 'Oculus|Meta' -or $model -match 'Quest')) {
        Say ('  This is a ' + $make + ' ' + $model + ', not a Meta Quest.  Carrying on; the app is made for Quest.') 'Yellow'
    }
}

function Installed-Version {
    $d = Shell ('dumpsys package ' + $AppId)
    if ($d -match 'versionName=(\S+)') { return $Matches[1] }
    return $null
}

# Every file under the game's folder on the headset: lowercase name -> bytes.
function Remote-Sizes {
    $map = @{}
    $out = Shell ('cd "' + $Remote + '" 2>/dev/null && find . -type f -exec stat -c "%s|%n" {} + 2>/dev/null')
    foreach ($line in ($out -split "`r?`n")) {
        if ($line -match '^(\d+)\|\./(.+)$') { $map[$Matches[2].ToLowerInvariant()] = [long]$Matches[1] }
    }
    return $map
}

# Folders adb makes belong to adb's user, and the game may not look into them;
# opened to everyone, as Descent's Setup learned on the headset.  The folders
# the app made itself (WritableDirs) are the app's already and are left alone.
function Open-AdbDirs {
    Shell ('cd "' + $Remote + '" 2>/dev/null && find . -type d -user shell -exec chmod 2775 {} + 2>/dev/null; true') | Out-Null
}

# The app makes its writable folders and takes in any name.restore Setup
# copied (ForsakenActivity.prepare).  Returns how many folders it can write.
function App-Prepare {
    $r = Run-Adb @('shell', 'am', 'broadcast', '-f', '32', '-n', ($AppId + '/.SetupReceiver'))
    if ($r.Out -match 'result=(\d+)') { return [int]$Matches[1] }
    Log ('  the app did not answer Setup: ' + $r.Out)
    return 0
}

# --- pilots, saved games and settings ---------------------------------------------------------

function Backup-Saves([string] $to) {
    $n = 0
    foreach ($item in $SaveItems) {
        $there = (Shell ('ls -d "' + $Remote + '/' + $item + '" 2>/dev/null')).Trim()
        if (-not $there -or $there -match 'No such') { continue }
        $dst = Join-Path $to ($item.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        $r = Run-Adb @('pull', ($Remote + '/' + $item), $dst)
        if ($r.Code -eq 0) { $n += @(Get-ChildItem -LiteralPath $dst -Recurse -File -ErrorAction SilentlyContinue).Count; if (Test-Path -LiteralPath $dst -PathType Leaf) { $n++ } }
        else { Log ('  could not copy ' + $item + ': ' + $r.Out) }
    }
    return $n
}

# Puts saved files back as name.restore; the app takes them in as its own.
function Restore-Saves([string] $from) {
    $n = 0
    foreach ($f in Get-ChildItem -LiteralPath $from -Recurse -File -ErrorAction SilentlyContinue) {
        $rel = $f.FullName.Substring($from.Length + 1).Replace('\', '/').ToLowerInvariant()
        $r = Run-Adb @('push', $f.FullName, ($Remote + '/' + $rel + '.restore'))
        if ($r.Code -eq 0) { $n++ }
    }
    if ($n) { App-Prepare | Out-Null }
    return $n
}

function Has-Pilot {
    $out = Shell ('ls "' + $Remote + '/pilots" 2>/dev/null')
    return [bool]($out.Trim() -and $out -notmatch 'No such')
}

# --- install ------------------------------------------------------------------------------

function Do-Install {
    Banner 'Setup'
    if (-not (Test-Path -LiteralPath $Apk)) { throw ('the app is missing from this folder: ' + $Apk) }

    # 1. The game, put together on this PC.
    Say '  Looking for Forsaken Remastered on this PC (optional: the soundtrack and intro)...'
    Search-Roots
    $rm = Find-Remaster
    if ($rm) { Log ('Forsaken Remastered: ' + $rm) } else { Log 'Forsaken Remastered: not found.  Forsaken VR does not need it.' }
    $Result['remaster'] = $rm
    Log ('The game for the headset is put together in ' + $Stage)
    $g = Stage-Game $rm
    if (-not $g.Ok) {
        Say ''
        Say '  The game data is not there, so there is nothing to put on the headset.' 'Yellow'
        $Result['result'] = 'no game data'
        Finish 1
    }
    $total = ($g.Files.Values | Measure-Object -Property Length -Sum).Sum
    Log ('the game: ' + $g.Files.Count + ' files, ' + (Size-Text $total))

    # 2. The headset.
    Get-Adb
    if (-not (Wait-Headset)) { Log 'No headset after waiting; nothing was changed.'; Finish 3 }
    Say ''
    Say '  Headset connected.' 'Green'
    Headset-Info

    # 3. Room for it all.
    $need = (Get-Item -LiteralPath $Apk).Length + $total + 50MB
    $f2 = (Shell 'df -k /sdcard | tail -1') -split '\s+'
    if ($f2.Count -ge 4 -and $f2[3] -match '^\d+$') {
        $free = [long]$f2[3] * 1KB
        Log ('free on the headset: ' + (Size-Text $free) + ', needed at most ' + (Size-Text $need))
        if ($free -lt $need) { throw ('the headset needs ' + (Size-Text $need) + ' free and has ' + (Size-Text $free) + '.  Remove something and run Setup again.') }
    }

    # 4. Pilots, saved games and settings: a copy on this PC first, every time.
    $was = Installed-Version
    $stamp = Get-Date -Format 'yyyy-MM-dd HHmm'
    if ($was) {
        Log ('installed now: ' + $AppName + ' ' + $was)
        $n = Backup-Saves (Join-Path $SaveRoot $stamp)
        if ($n) { Log ('  ' + $n + ' pilot, save and settings files copied to ' + (Join-Path $SaveRoot $stamp)) }
    }

    # 5. The app.
    Say ''
    Say ('  Installing ' + $AppName + ' ' + $Version + ' on the headset...')
    $r = Run-Adb @('install', '-r', $Apk)
    if ($r.Out -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE|signatures do not match') {
        # A build signed with another key (a test build) is in the way: it has
        # to go first, which also clears its files.  The saves are on the PC.
        Log '  A different build of the app is on the headset; replacing it.'
        $u = Run-Adb @('uninstall', $AppId)
        Log ('  removed: ' + $u.Out)
        $r = Run-Adb @('install', $Apk)
    }
    if ($r.Code -ne 0 -or $r.Out -notmatch 'Success') { throw ('the app did not install: ' + $r.Out) }
    Log ('  installed (' + ($r.Out -split "`r?`n" | Select-Object -Last 1) + ')')

    # 6. The app's own folders (the ones the game writes), made by the app.
    $w = App-Prepare
    Log ('  the app made its folders: ' + $w + ' of ' + $WritableDirs.Count + ' writable')
    if ($w -lt $WritableDirs.Count) { Log '  (settings, pilots or saved games may not be kept: Collect logs.bat and send us the zip)' }

    # 7. The game's files: only what is missing or changed.
    $have = Remote-Sizes
    $todo = @($g.Files.Keys | Where-Object { -not $have.ContainsKey($_) -or $have[$_] -ne $g.Files[$_].Length } | Sort-Object)
    if (-not $todo) { Log '  the game''s files are already on the headset' }
    else {
        $bytes = ($todo | ForEach-Object { $g.Files[$_].Length } | Measure-Object -Sum).Sum
        Say ''
        Say ('  Copying the game to the headset (' + $todo.Count + ' files, ' + (Size-Text $bytes) + ')...')
        # Whole folders the headset does not have yet go over as one archive each,
        # unpacked on the headset by its own tar: much quicker than file by file,
        # and adb push may not make folders inside the app's storage on Android 13
        # and later (a shell on the headset may).  The archive is made lowercase
        # on this PC first.
        $missingTop = @($todo | ForEach-Object { ($_ -split '/')[0] } | Sort-Object -Unique | Where-Object { $t = $_; -not ($have.Keys | Where-Object { $_ -like ($t + '/*') -or $_ -eq $t } | Select-Object -First 1) })
        $done = @{}
        $tarExe = Join-Path $env:SystemRoot 'System32\tar.exe'
        foreach ($top in $missingTop) {
            $files = @($todo | Where-Object { $_ -like ($top + '/*') })
            if ($files.Count -lt 20 -or -not (Test-Path -LiteralPath $tarExe)) { continue }
            $lcRoot = Join-Path $Stage 'lc'
            $lc = Join-Path $lcRoot $top
            if (Test-Path -LiteralPath $lc) { Remove-Item -LiteralPath $lc -Recurse -Force }
            foreach ($k in $files) {
                $dst = Join-Path $lcRoot ($k.Replace('/', '\'))
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
                Copy-Item -LiteralPath $g.Files[$k].FullName -Destination $dst -Force
            }
            $tar = Join-Path $Stage ($top + '.tar')
            & $tarExe -cf $tar -C $lcRoot $top
            if ($LASTEXITCODE) { throw ('packing ' + $top + ' failed') }
            Say ('    ' + $top + '/  (' + $files.Count + ' files)')
            $tmpRemote = '/data/local/tmp/forsaken-vr-' + $top + '.tar'
            $p = Run-Adb @('push', $tar, $tmpRemote)
            if ($p.Code -ne 0) { throw ('copying ' + $top + ' failed: ' + $p.Out) }
            $x = Shell ('cd "' + $Remote + '" && tar -xf "' + $tmpRemote + '" && echo UNPACKED; rm -f "' + $tmpRemote + '"')
            if ($x -notmatch 'UNPACKED') { throw ('unpacking ' + $top + ' on the headset failed: ' + $x) }
            foreach ($k in $files) { $done[$k] = $true }
            Remove-Item -LiteralPath $lc -Recurse -Force
            Remove-Item -LiteralPath $tar -Force
        }
        $rest = @($todo | Where-Object { -not $done.ContainsKey($_) })
        # The folders for single files, made by a shell on the headset (see above).
        $parents = @($rest | ForEach-Object { $i = $_.LastIndexOf('/'); if ($i -gt 0) { $_.Substring(0, $i) } } | Sort-Object -Unique)
        if ($parents) { Shell ('cd "' + $Remote + '" && mkdir -p ' + (($parents | ForEach-Object { '"' + $_ + '"' }) -join ' ')) | Out-Null }
        $i = 0
        foreach ($k in $rest) {
            $i++
            Say ('    ' + $i + '/' + $rest.Count + '  ' + $k)
            $p = Run-Adb @('push', $g.Files[$k].FullName, ($Remote + '/' + $k))
            if ($p.Code -ne 0) { throw ('copying ' + $k + ' failed: ' + $p.Out) }
        }
        Open-AdbDirs
        # Check what arrived, by size.
        $have = Remote-Sizes
        $bad = @($g.Files.Keys | Where-Object { -not $have.ContainsKey($_) -or $have[$_] -ne $g.Files[$_].Length })
        if ($bad) { throw ($bad.Count + ' files did not arrive whole (' + (($bad | Select-Object -First 5) -join ', ') + ').  Run Setup again.') }
        Log ('  ' + $todo.Count + ' files copied and checked')
    }

    # 8. The settings, the first time only (the player's own after that).
    $tmpl = Join-Path $g.Pc 'configs\debug.txt'
    if ((Test-Path -LiteralPath $tmpl) -and -not $have.ContainsKey('configs/debug.txt')) {
        $r = Run-Adb @('push', $tmpl, ($Remote + '/configs/debug.txt.restore'))
        if ($r.Code -eq 0) { App-Prepare | Out-Null; Log '  settings: the defaults put in place' }
    }

    # 9. The pilots back, if the headset has none now (a replaced build).
    $dirs = @(Get-ChildItem -LiteralPath $SaveRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
    if ($dirs -and -not (Has-Pilot)) {
        $n = Restore-Saves $dirs[0].FullName
        if ($n) { Log ('  ' + $n + ' pilot, save and settings files put back (from ' + $dirs[0].Name + ')') }
    }

    Say ''
    Log 'Done.'
    Say ''
    if ($IsFrame) {
        Say '  On the Frame: with Lepton Development running, double-click' 'Green'
        Say '  "Start on Steam Frame.bat".' 'Green'
    } else {
        Say '  On the headset: Library, then Unknown Sources, then Forsaken VR.' 'Green'
        Say '  You can unplug the headset now.'
    }
    if (Test-Path -LiteralPath $SaveRoot) { Say ('  Pilots, saves and settings are copied to ' + $SaveRoot + ' each time Setup runs.') }
    Finish 0
}

# --- uninstall ----------------------------------------------------------------------------

function Do-Uninstall {
    Banner 'Uninstall'
    Get-Adb
    if (-not (Wait-Headset)) { Log 'No headset; nothing was changed.'; Finish 3 }
    Headset-Info
    $was = Installed-Version
    if (-not $was) { Log ('  ' + $AppName + ' is not on this headset.'); Finish 0 }
    $stamp = Get-Date -Format 'yyyy-MM-dd HHmm'
    $kept = Backup-Saves (Join-Path $SaveRoot $stamp)
    if ($kept) { Log ('  ' + $kept + ' pilot, save and settings files copied to this PC') }
    $u = Run-Adb @('uninstall', $AppId)
    if ($u.Out -notmatch 'Success') { throw ('the app was not removed: ' + $u.Out) }
    Log ('  ' + $AppName + ' ' + $was + ' removed from the headset, with the game files it was given.')
    if ($kept) { Say ('  Your pilots, saved games and settings are kept in ' + $SaveRoot + '; Setup puts them back.') }
    Say ('  The game put together for the headset stays on this PC (' + $Stage + '),')
    Say '  so a new install needs no download.  Delete that folder to free the space.'
    Log 'Done.'
    Finish 0
}

# --- collect logs -------------------------------------------------------------------------

function Do-Collect {
    Banner 'Collect logs'
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('forsaken-vr-quest-logs-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    foreach ($f in @((Join-Path (Split-Path -Parent $QuestHere) 'setup-log.txt'), (Join-Path ([System.IO.Path]::GetTempPath()) 'forsaken-vr-quest-install.txt'), (Join-Path ([System.IO.Path]::GetTempPath()) 'forsaken-vr-quest-uninstall.txt'))) {
        if (Test-Path -LiteralPath $f) { Copy-Item -LiteralPath $f -Destination $tmp }
    }
    Get-Adb
    if (Wait-Headset) {
        Headset-Info
        $v = Installed-Version
        Log ('installed: ' + $(if ($v) { $AppName + ' ' + $v } else { 'not installed' }))
        $to = Join-Path $tmp 'headset'
        New-Item -ItemType Directory -Force -Path (Join-Path $to 'logs') | Out-Null
        # The newest five game logs, the crash reports and the settings.
        $logs = ((Shell ('ls -t "' + $Remote + '/logs" 2>/dev/null | head -5')) -split "`r?`n") | Where-Object { $_.Trim() -and $_ -notmatch 'No such' }
        foreach ($n in $logs) { Run-Adb @('pull', ($Remote + '/logs/' + $n.Trim()), (Join-Path (Join-Path $to 'logs') $n.Trim())) | Out-Null }
        foreach ($n in @('crash.txt', 'crash.prev.txt', 'configs/debug.txt')) {
            Run-Adb @('pull', ($Remote + '/' + $n), (Join-Path $to ($n.Replace('/', '_')))) | Out-Null
        }
        # What the folder holds, by name and size (no file contents).
        Set-Content -LiteralPath (Join-Path $to 'files.txt') -Value (Shell ('ls -lR "' + $Remote + '" 2>&1 | head -400')) -Encoding UTF8
        # The app's own lines and any crash, nothing from other apps.
        $lc = (Run-Adb @('logcat', '-d', '-v', 'time', '-s', 'ForsakenVR:*', 'SDL:*', 'SDL/APP:*', 'OpenXR-Loader:*', 'openal:*', 'AndroidRuntime:*', 'DEBUG:*', 'libc:F')).Out
        Set-Content -LiteralPath (Join-Path $tmp 'logcat.txt') -Value $lc -Encoding UTF8
    } else {
        Log 'No headset connected: only this PC''s logs are collected.'
    }
    Copy-Item -LiteralPath $LogFile -Destination $tmp
    $zip = Join-Path ([Environment]::GetFolderPath('Desktop')) ($ProductFile + $(if ($IsFrame) { ' Steam Frame' } else { ' Quest' }) + ' logs.zip')
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip
    Remove-Item -LiteralPath $tmp -Recurse -Force
    $Result['zip'] = $zip
    Say ''
    Say ('  Logs saved to ' + $zip) 'Green'
    Say '  Send that file to us (Discord or GitHub).'
    if (-not $NoPrompt) { try { Start-Process explorer.exe ('/select,"' + $zip + '"') } catch { } }
    Finish 0
}

# --- detect -------------------------------------------------------------------------------

function Do-Detect {
    Search-Roots
    $rm = Find-Remaster
    $Result['remaster'] = $rm
    $Result['staged'] = Test-Path -LiteralPath (Join-Path $Stage 'pc\Data\levels')
    Log ('Forsaken Remastered: ' + $(if ($rm) { $rm } else { 'not found' }))
    $script:AdbExe = Find-Adb
    if ($AdbExe) {
        $script:WaitSeconds = 0
        if (Wait-Headset) { Headset-Info; $v = Installed-Version; $Result['installed'] = $v; Log ('installed: ' + $v) }
        else { $Result['headset'] = $null; Log 'headset: none ready' }
    } else { Log 'adb: none on this PC yet (Setup downloads it)' }
    Finish 0
}

# Starts the app on the headset (a Steam Frame's Lepton shows no launcher for it).
function Do-Start {
    Banner 'Start'
    Get-Adb
    if (-not (Wait-Headset)) { Log 'No headset; nothing was started.'; Finish 3 }
    $r = Run-Adb @('shell', 'am', 'start', '-n', $Activity)
    Log ('started: ' + $r.Out)
    Say '  Started.  Put the headset on.' 'Green'
    Finish 0
}

switch ($Mode) {
    'start'     { Do-Start }
    'uninstall' { Do-Uninstall }
    'collect'   { Do-Collect }
    'detect'    { Do-Detect }
    default     { Do-Install }
}
