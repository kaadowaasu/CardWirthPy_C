[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Offline,
    [string]$Serial = $env:ANDROID_SERIAL,
    [string]$GradleExecutable = "",
    [string]$KeyStore = "",
    [string]$KeyAlias = "",
    [string]$StorePassword = "",
    [string]$KeyPassword = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$androidRoot = Join-Path $repoRoot "src\platform\android"
$distAndroidRoot = Join-Path $repoRoot "dist\android"
$userDataRoot = Join-Path $distAndroidRoot "UserData"
$apkBuildRoot = Join-Path $androidRoot "app\build"
$buildOutputDirectory = Join-Path $apkBuildRoot "outputs\apk\all\release"
$alignedApk = Join-Path $buildOutputDirectory "app-all-release-aligned.apk"
$signedBuildApk = Join-Path $buildOutputDirectory "app-all-release-signed.apk"
$distApk = Join-Path $distAndroidRoot "CardWirthPy_C-ALL-release.apk"
$activityName = "jp.cardwirth.sdlsample.BootstrapActivity"

if (-not (Test-Path -LiteralPath $userDataRoot -PathType Container)) {
    throw "Android UserData source is missing: $userDataRoot"
}

function Resolve-Gradle {
    if (-not [string]::IsNullOrWhiteSpace($GradleExecutable)) {
        $candidate = [System.IO.Path]::GetFullPath($GradleExecutable)
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Gradle executable was not found: $candidate"
        }
        return $candidate
    }

    # ラッパーが固定する Gradle のキャッシュを先に使う
    # 。取得済みならローカルの再ビルドをオフラインで行える。
    $cachedRoot = Join-Path $env:USERPROFILE ".gradle\wrapper\dists"
    if (Test-Path -LiteralPath $cachedRoot -PathType Container) {
        $cached = Get-ChildItem -LiteralPath $cachedRoot -Filter "gradle.bat" -File -Recurse |
            Where-Object { $_.FullName -match "\\gradle-8\.12\\bin\\gradle\.bat$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $cached) {
            return $cached.FullName
        }
    }

    # キャッシュがなければ、固定版のラッパーが通
    # 常の Gradle のダウンロードを行う。
    $wrapper = Join-Path $androidRoot "gradlew.bat"
    if (Test-Path -LiteralPath $wrapper -PathType Leaf) {
        return $wrapper
    }

    $command = Get-Command gradle.bat -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    throw "Neither src/platform/android/gradlew.bat nor gradle.bat is available."
}

function Resolve-AndroidTool {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $sdkRoot = if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_SDK_ROOT)) {
        $env:ANDROID_SDK_ROOT
    } elseif (-not [string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
        $env:ANDROID_HOME
    } else {
        Join-Path $env:LOCALAPPDATA "Android\Sdk"
    }
    $candidate = Join-Path $sdkRoot $RelativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Android SDK tool was not found: $candidate"
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$gradle = Resolve-Gradle
$null = New-Item -ItemType Directory -Path $distAndroidRoot -Force

$env:ANDROID_HOME = if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
    $env:ANDROID_HOME
} else {
    Join-Path $env:LOCALAPPDATA "Android\Sdk"
}
$env:ANDROID_SDK_ROOT = if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_SDK_ROOT)) {
    $env:ANDROID_SDK_ROOT
} else {
    $env:ANDROID_HOME
}

Push-Location $androidRoot
try {
    $gradleArguments = @("assembleAllRelease", "--console=plain")
    if ($Offline) { $gradleArguments += "--offline" }
    Invoke-Native -Executable $gradle `
        -Arguments $gradleArguments `
        -Description "Gradle assembleAllRelease"
} finally {
    Pop-Location
}

$unsignedCandidates = @(
    Get-ChildItem -LiteralPath $apkBuildRoot -Filter "app-all-release-unsigned.apk" -File -Recurse |
        Sort-Object LastWriteTimeUtc -Descending
)
if ($unsignedCandidates.Count -eq 0) {
    throw "Release APK was not generated under: $apkBuildRoot"
}
$unsignedApk = $unsignedCandidates[0].FullName
Write-Host "Unsigned APK: $unsignedApk"
$null = New-Item -ItemType Directory -Path $buildOutputDirectory -Force

$zipalign = Resolve-AndroidTool -RelativePath "build-tools\35.0.0\zipalign.exe"
$apksigner = Resolve-AndroidTool -RelativePath "build-tools\35.0.0\apksigner.bat"

if ([string]::IsNullOrWhiteSpace($KeyStore)) {
    $KeyStore = Join-Path $env:USERPROFILE ".android\debug.keystore"
    if ([string]::IsNullOrWhiteSpace($KeyAlias)) { $KeyAlias = "androiddebugkey" }
    if ([string]::IsNullOrWhiteSpace($StorePassword)) { $StorePassword = "android" }
    if ([string]::IsNullOrWhiteSpace($KeyPassword)) { $KeyPassword = "android" }
} elseif ([string]::IsNullOrWhiteSpace($KeyAlias) -or
          [string]::IsNullOrWhiteSpace($StorePassword) -or
          [string]::IsNullOrWhiteSpace($KeyPassword)) {
    throw "A custom KeyStore requires KeyAlias, StorePassword, and KeyPassword."
}
if (-not (Test-Path -LiteralPath $KeyStore -PathType Leaf)) {
    throw "Signing keystore was not found: $KeyStore"
}

if (Test-Path -LiteralPath $alignedApk) { Remove-Item -LiteralPath $alignedApk -Force }
if (Test-Path -LiteralPath $signedBuildApk) { Remove-Item -LiteralPath $signedBuildApk -Force }

Invoke-Native -Executable $zipalign `
    -Arguments @("-f", "-p", "4", $unsignedApk, $alignedApk) `
    -Description "zipalign"
Invoke-Native -Executable $apksigner `
    -Arguments @(
        "sign", "--ks", $KeyStore, "--ks-key-alias", $KeyAlias,
        "--ks-pass", "pass:$StorePassword", "--key-pass", "pass:$KeyPassword",
        "--out", $signedBuildApk, $alignedApk
    ) `
    -Description "apksigner sign"
Invoke-Native -Executable $apksigner `
    -Arguments @("verify", "--verbose", $signedBuildApk) `
    -Description "apksigner verify"

Copy-Item -LiteralPath $signedBuildApk -Destination $distApk -Force
$artifact = Get-Item -LiteralPath $distApk
$hash = (Get-FileHash -LiteralPath $distApk -Algorithm SHA256).Hash
Write-Host "APK: $($artifact.FullName)"
Write-Host "Size: $($artifact.Length) bytes"
Write-Host "SHA-256: $hash"

if ($Install) {
    # パッケージの識別子は成果物から読み、導入・起動を共通 Gradle
    # 設定に従わせる。別の固定 ID を持たない。
    $aapt = Resolve-AndroidTool -RelativePath "build-tools\35.0.0\aapt.exe"
    $badging = & $aapt dump badging $distApk
    if ($LASTEXITCODE -ne 0) { throw "Cannot read the APK package identity." }
    $packageMatch = [regex]::Match(($badging -join "`n"), "(?m)^package: name='([^']+)'")
    if (-not $packageMatch.Success) { throw "APK package identity is missing." }
    $packageName = $packageMatch.Groups[1].Value
    $adb = Resolve-AndroidTool -RelativePath "platform-tools\adb.exe"
    if ([string]::IsNullOrWhiteSpace($Serial)) {
        $devices = @(
            & $adb devices |
                ForEach-Object {
                    if ($_ -match '^([^\s]+)\s+device(?:\s|$)') { $Matches[1] }
                }
        )
        if ($devices.Count -ne 1) {
            throw "Specify -Serial because exactly one authorized device was not found. Found: $($devices -join ', ')"
        }
        $Serial = $devices[0]
    }

    Invoke-Native -Executable $adb `
        -Arguments @("-s", $Serial, "install", "-r", "-t", $distApk) `
        -Description "adb install -r -t"
    Invoke-Native -Executable $adb `
        -Arguments @("-s", $Serial, "shell", "am", "force-stop", $packageName) `
        -Description "Stopping CardWirthPy_C ALL"
    Invoke-Native -Executable $adb `
        -Arguments @(
            "-s", $Serial, "shell", "am", "start", "-W", "-n",
            "$packageName/$activityName"
        ) `
        -Description "Starting CardWirthPy_C ALL"
}
