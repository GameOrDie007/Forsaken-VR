# Builds the standalone Android app (Quest 3; the Steam Frame through Lepton):
# the native libraries for arm64 (and x86_64 for the desk emulator) with the
# NDK's CMake, copied into platform\quest\app\libs, then the APK with Gradle.
# -Release: the headsets only, signed with the Game Or Die key named by
# $env:GOD_KEYSTORE (default keys\keystore.properties in the Android tools folder, never in the repository).
# The Android tools folder (sdk, jdk17, src with the libraries' release sources, keys) is
# $env:ANDROID_TOOLS, or a folder named Android two levels above the repository.
# Usage: powershell -ExecutionPolicy Bypass -File tools\build-quest.ps1 [-NoX64] [-Release] [-NativeOnly]
param([switch] $NoX64, [switch] $Release, [switch] $NativeOnly)
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).ProviderPath
$Tools = if ($env:ANDROID_TOOLS) { $env:ANDROID_TOOLS } else { [IO.Path]::GetFullPath((Join-Path $Repo '..\..\Android')) }
if ($Release) {
    $NoX64 = $true
    if (-not $env:GOD_KEYSTORE) { $env:GOD_KEYSTORE = Join-Path $Tools 'keys\keystore.properties' }
    if (-not (Test-Path -LiteralPath $env:GOD_KEYSTORE)) { throw "no release key: $env:GOD_KEYSTORE" }
}
$ErrorActionPreference = 'Stop'
$Sdk = Join-Path $Tools 'sdk'
$Cmake = "$Sdk\cmake\3.22.1\bin"
$Ndk = (Get-ChildItem "$Sdk\ndk" -Directory | Sort-Object Name | Select-Object -Last 1).FullName
$env:JAVA_HOME = Join-Path $Tools 'jdk17'
$Quest = Join-Path $Repo 'platform\quest'

$abis = @('arm64-v8a')
if (-not $NoX64) { $abis += 'x86_64' }
foreach ($abi in $abis) {
    $libs = Join-Path $Quest "app\libs\$abi"
    New-Item -ItemType Directory -Force $libs | Out-Null
    $b = Join-Path $Repo ('build\quest-' + $(if ($abi -eq 'x86_64') { 'x64' } else { 'arm64' }))
    if (-not (Test-Path (Join-Path $b 'build.ninja'))) {
        & "$Cmake\cmake.exe" -G Ninja "-DCMAKE_MAKE_PROGRAM=$Cmake\ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake" `
            "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release `
            ('-DEXT_SRC=' + (Join-Path $Tools 'src').Replace('\', '/')) `
            -S (Join-Path $Quest 'jni') -B $b | Out-Null
        if ($LASTEXITCODE) { throw "configure $abi failed" }
    }
    & "$Cmake\ninja.exe" -C $b
    if ($LASTEXITCODE) { throw "build $abi failed" }
    $triple = if ($abi -eq 'x86_64') { 'x86_64-linux-android' } else { 'aarch64-linux-android' }
    Copy-Item (Join-Path $b 'SDL2\libSDL2.so'), (Join-Path $b 'openal\libopenal.so'), (Join-Path $b 'libmain.so') $libs -Force
    if ($Release) {
        # The headsets get the libraries without their debug symbols (OpenAL
        # Soft alone is 38 MB with them); the build folder keeps the full ones
        # for reading a crash report's addresses.
        $strip = "$Ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
        foreach ($so in @('libSDL2.so', 'libopenal.so', 'libmain.so')) {
            & $strip --strip-unneeded (Join-Path $libs $so)
            if ($LASTEXITCODE) { throw "strip $so failed" }
        }
    }
    Copy-Item "$Ndk\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\$triple\libc++_shared.so" $libs -Force
    Copy-Item (Join-Path $Tools "src\openxr\aar\jni\$abi\libopenxr_loader.so") $libs -Force
}
if ($NativeOnly) { return }
Set-Content -LiteralPath (Join-Path $Quest 'local.properties') -Value ('sdk.dir=' + ($Sdk -replace '\\', '/' -replace ':', '\:')) -Encoding ASCII
Push-Location $Quest
try {
    # Gradle's notes go to stderr, which PowerShell 5.1 turns into errors: judge it by its exit code.
    $ErrorActionPreference = 'Continue'
    & (Join-Path $Quest 'gradlew.bat') --no-daemon -q $(if ($Release) { 'assembleRelease' } else { 'assembleDebug' }) 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -notmatch '^Note: ' }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE) { throw 'gradle failed' }
} finally { Pop-Location }
$apk = Join-Path $Quest $(if ($Release) { 'app\build\outputs\apk\release\app-release.apk' } else { 'app\build\outputs\apk\debug\app-debug.apk' })
'APK: {0} ({1:N1} MB)' -f $apk, ((Get-Item $apk).Length / 1MB)
