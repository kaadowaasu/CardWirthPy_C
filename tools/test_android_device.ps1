[CmdletBinding()]
param(
    [string]$Serial = $env:ANDROID_SERIAL,
    [switch]$SkipBuild,
    [ValidateRange(1, 120)]
    [int]$SmokeTimeoutSeconds = 45,
    [ValidateRange(0, 30)]
    [int]$LaunchSettleSeconds = 6,
    [string]$OutputDirectory = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$androidRoot = Join-Path $repoRoot "src\platform\android"
$distAndroidRoot = Join-Path $repoRoot "dist\android"
$buildApkPath = Join-Path $androidRoot "app\build\outputs\apk\all\debug\app-all-debug.apk"
$apkPath = Join-Path $distAndroidRoot "CardWirthPy_C-ALL-debug.apk"
$packageName = "net.webcardwirth.cwsengoku.cardwirthpy_c_app"
$activityName = "jp.cardwirth.sdlsample.BootstrapActivity"
$argumentsExtra = "jp.cardwirth.cardwirthc.test.ARGUMENTS"
$reportRelativePath = "files/hosted-smoke.txt"
$reportAbsolutePath = "/data/user/0/$packageName/$reportRelativePath"
$layoutReportRelativePath = "files/hosted-layout.txt"
$layoutReportAbsolutePath = "/data/user/0/$packageName/$layoutReportRelativePath"
$zoomReportRelativePath = "files/editor-zoom.txt"
$zoomReportAbsolutePath = "/data/user/0/$packageName/$zoomReportRelativePath"

$sdkRoot = if ($env:ANDROID_SDK_ROOT) {
    $env:ANDROID_SDK_ROOT
} elseif ($env:ANDROID_HOME) {
    $env:ANDROID_HOME
} else {
    Join-Path $env:LOCALAPPDATA "Android\Sdk"
}
$adbCandidate = Join-Path $sdkRoot "platform-tools\adb.exe"
if (Test-Path -LiteralPath $adbCandidate) {
    $adbPath = (Resolve-Path -LiteralPath $adbCandidate).Path
} else {
    $adbCommand = Get-Command adb.exe -ErrorAction Stop
    $adbPath = $adbCommand.Source
}

if ([string]::IsNullOrWhiteSpace($Serial)) {
    $devices = @(
        & $adbPath devices |
            ForEach-Object {
                if ($_ -match '^([^\s]+)\s+device(?:\s|$)') { $Matches[1] }
            }
    )
    if ($devices.Count -ne 1) {
        throw "Specify -Serial because exactly one authorized device was not found. Found: $($devices -join ', ')"
    }
    $Serial = $devices[0]
}

$script:AdbPath = $adbPath
$script:DeviceSerial = $Serial
$script:PackageName = $packageName
$script:ActivityName = $activityName
$script:ArgumentsExtra = $argumentsExtra
$script:ReportRelativePath = $reportRelativePath
$script:OutputDirectory = $OutputDirectory
$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [string[]]$AdbArguments,
        [switch]$AllowFailure
    )

    # Windows PowerShell はネイティブの標準エラーを ErrorRecord
    # に変換する。run-as ... cat などの監視コマンドは報告
    # ファイルができるまで正常に失敗し得るため、-AllowFailure
    # が終了値を見る前に全体の Stop 方針で終了しないようにする。
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $lines = @(& $script:AdbPath -s $script:DeviceSerial @AdbArguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    $output = ($lines | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "adb $($AdbArguments -join ' ') failed with exit code ${exitCode}:`n$output"
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Write-TestText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [AllowEmptyString()]
        [string]$Content
    )

    $path = Join-Path $script:OutputDirectory $Name
    [System.IO.File]::WriteAllText($path, $Content, $script:Utf8NoBom)
    $path
}

function Stop-App {
    $null = Invoke-Adb @("shell", "am", "force-stop", $script:PackageName)
}

function Clear-DeviceLogs {
    $null = Invoke-Adb @("logcat", "-c")
    $null = Invoke-Adb @("logcat", "-b", "crash", "-c")
}

function Set-GlobalSetting {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowEmptyString()][string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "null") {
        $null = Invoke-Adb @("shell", "settings", "delete", "global", $Name)
    } else {
        $null = Invoke-Adb @("shell", "settings", "put", "global", $Name, $Value)
    }
}

function Wake-Device {
    $null = Invoke-Adb @("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    Start-Sleep -Milliseconds 500
    $null = Invoke-Adb @("shell", "wm", "dismiss-keyguard") -AllowFailure
    Start-Sleep -Milliseconds 500
    $policy = (Invoke-Adb @("shell", "dumpsys", "window", "policy")).Output
    $secureKeyguard =
        $policy -match '(?m)^\s*showing=true\s*$' -and
        $policy -match '(?m)^\s*secure=true\s*$' -and
        $policy -match '(?m)^\s*mInputRestricted=true\s*$'
    if ($secureKeyguard) {
        throw "The device is showing a secure lock screen. Unlock it physically, keep it connected, and rerun this test."
    }
}

function Start-App {
    param([string[]]$NativeArguments = @())

    Wake-Device
    $component = $script:PackageName + "/" + $script:ActivityName
    $startArguments = @("shell", "am", "start", "-W", "-n", $component)
    if ($NativeArguments.Count -gt 0) {
        $startArguments += @(
            "--esa",
            $script:ArgumentsExtra,
            ($NativeArguments -join ",")
        )
    }
    $result = Invoke-Adb $startArguments
    Write-Host $result.Output
}

function Wait-AppProcess {
    param([ValidateRange(1, 60)][int]$TimeoutSeconds = 20)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $pidResult = Invoke-Adb @("shell", "pidof", $script:PackageName) -AllowFailure
        if ($pidResult.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($pidResult.Output)) {
            return $pidResult.Output.Trim()
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "The process $($script:PackageName) did not become active within $TimeoutSeconds seconds."
}

function Assert-ActivityRunning {
    param([Parameter(Mandatory = $true)][string]$Phase)

    $processId = Wait-AppProcess
    $activities = (Invoke-Adb @("shell", "dumpsys", "activity", "activities")).Output
    $component = $script:PackageName + "/" + $script:ActivityName
    if ($activities.IndexOf($component, [StringComparison]::Ordinal) -lt 0) {
        throw "The expected activity $component is not present during $Phase."
    }
    $state = "phase=$Phase`npid=$processId`ncomponent=$component`n"
    $null = Write-TestText -Name "$Phase-state.txt" -Content $state
    Write-Host "$Phase process=$processId activity=$component"
}

function Assert-UserDataLayout {
    $expectedDirectories = @(
        "files/Data",
        "files/Scenario",
        "files/Yado",
        "files/Config",
        "files/Database",
        "files/Temp",
        "files/Cache",
        "files/Trash",
        "files/app/Data",
        "files/app/resource"
    )
    $listing = Invoke-Adb (@("shell", "run-as", $script:PackageName, "ls", "-ld") + $expectedDirectories)
    $expectedSeedFiles = @(
        "files/Yado/新規宿(2)/Environment.xml",
        "files/Yado/新規宿(2)/Party/ジュリア一行/Party.xml",
        "files/Scenario/ゴブリンの洞窟/Summary.wsm",
        "files/app/Data/Font/NotoSansCJKjp-Regular.otf"
    )
    # adb shell は末尾の引数をコマンド文字
    # 列に組み直す。宿名の括弧などシェルの特殊文字を
    # 含む日本語パスを、リテラルの単一引用符で囲む。
    $quotedSeedFiles = @($expectedSeedFiles | ForEach-Object { "'$_'" })
    $seedListing = Invoke-Adb (
        @("shell", "run-as", $script:PackageName, "ls", "-l") + $quotedSeedFiles
    )
    $content = $listing.Output + "`n" + $seedListing.Output + "`n"
    $null = Write-TestText -Name "userdata-layout.txt" -Content $content
    Write-Host "Writable UserData, bundled Japanese font, inn, and scenario seed are present."
}

function Set-SystemSetting {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowEmptyString()][string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "null") {
        $null = Invoke-Adb @("shell", "settings", "delete", "system", $Name)
    } else {
        $null = Invoke-Adb @("shell", "settings", "put", "system", $Name, $Value)
    }
}

function Get-DisplayFrame {
    $windowState = (Invoke-Adb @("shell", "dumpsys", "window", "displays")).Output
    $match = [regex]::Match(
        $windowState,
        '(?m)^\s*DisplayFrames w=(\d+) h=(\d+) r=(\d+)\s*$'
    )
    if (-not $match.Success) {
        throw "Unable to read the active Android display frame."
    }
    [pscustomobject]@{
        Width = [int]$match.Groups[1].Value
        Height = [int]$match.Groups[2].Value
        Rotation = [int]$match.Groups[3].Value
        WindowState = $windowState
    }
}

function Assert-LandscapeLock {
    $savedAccelerometer = (
        Invoke-Adb @("shell", "settings", "get", "system", "accelerometer_rotation")
    ).Output.Trim()
    $savedUserRotation = (
        Invoke-Adb @("shell", "settings", "get", "system", "user_rotation")
    ).Output.Trim()
    $observations = New-Object System.Collections.Generic.List[string]

    try {
        Set-SystemSetting -Name "accelerometer_rotation" -Value "0"
        foreach ($portraitRotation in @("0", "2")) {
            Set-SystemSetting -Name "user_rotation" -Value $portraitRotation
            Start-Sleep -Seconds 2
            $frame = Get-DisplayFrame
            $activities = (Invoke-Adb @("shell", "dumpsys", "activity", "activities")).Output
            $component = $script:PackageName + "/" + $script:ActivityName
            if ($activities.IndexOf($component, [StringComparison]::Ordinal) -lt 0) {
                throw "The expected activity disappeared during the rotation challenge."
            }
            if ($frame.Width -le $frame.Height) {
                throw "Portrait rotation $portraitRotation changed the app to $($frame.Width)x$($frame.Height)."
            }
            $orientationPattern =
                'source=ActivityRecord\{[^\r\n]*' + [regex]::Escape($component) +
                '[^\r\n]*\}\s+SCREEN_ORIENTATION_LANDSCAPE'
            if ($frame.WindowState -notmatch $orientationPattern) {
                throw "MainActivity did not retain SCREEN_ORIENTATION_LANDSCAPE during rotation challenge $portraitRotation."
            }
            $observations.Add(
                "requested_user_rotation=$portraitRotation requested_orientation=SCREEN_ORIENTATION_LANDSCAPE width=$($frame.Width) height=$($frame.Height) display_rotation=$($frame.Rotation)"
            )
        }
    } finally {
        Set-SystemSetting -Name "user_rotation" -Value $savedUserRotation
        Set-SystemSetting -Name "accelerometer_rotation" -Value $savedAccelerometer
    }

    Start-Sleep -Seconds 1
    $restoredFrame = Get-DisplayFrame
    if ($restoredFrame.Width -le $restoredFrame.Height) {
        throw "The app left landscape after restoring the device rotation settings."
    }
    $observations.Add(
        "restored_accelerometer_rotation=$savedAccelerometer restored_user_rotation=$savedUserRotation width=$($restoredFrame.Width) height=$($restoredFrame.Height) display_rotation=$($restoredFrame.Rotation)"
    )
    $null = Write-TestText -Name "orientation-lock.txt" -Content (($observations -join "`n") + "`n")
    Write-Host "Landscape lock passed both portrait rotation challenges; device rotation settings restored."
}

function Save-Screenshot {
    param([Parameter(Mandatory = $true)][string]$Phase)

    $remotePath = "/data/local/tmp/cardwirthc-$Phase-$PID.png"
    $localPath = Join-Path $script:OutputDirectory "$Phase.png"
    $null = Invoke-Adb @("shell", "screencap", "-p", $remotePath)
    try {
        $null = Invoke-Adb @("pull", $remotePath, $localPath)
    } finally {
        $null = Invoke-Adb @("shell", "rm", "-f", $remotePath) -AllowFailure
    }
    $image = Get-Item -LiteralPath $localPath
    if ($image.Length -lt 1024) {
        throw "Screenshot $localPath is unexpectedly small ($($image.Length) bytes)."
    }
    Write-Host "Saved screenshot: $localPath"
}

function Assert-NoCrash {
    param([Parameter(Mandatory = $true)][string]$Phase)

    $crashLog = (Invoke-Adb @("logcat", "-b", "crash", "-d", "-v", "threadtime")).Output
    $normalLog = (Invoke-Adb @("logcat", "-d", "-v", "threadtime")).Output
    $null = Write-TestText -Name "$Phase-crash.log" -Content ($crashLog + "`n")
    $null = Write-TestText -Name "$Phase-logcat.log" -Content ($normalLog + "`n")

    $packagePattern = [regex]::Escape($script:PackageName)
    $fatalPattern = "FATAL EXCEPTION|Fatal signal [0-9]+|SIGSEGV|SIGABRT|Abort message:|backtrace:"
    $crashMentionsApp = $crashLog -match $packagePattern
    $normalMentionsFatalApp =
        $normalLog -match $packagePattern -and $normalLog -match $fatalPattern
    if (($crashMentionsApp -and $crashLog -match $fatalPattern) -or $normalMentionsFatalApp) {
        throw "A Java or native crash signature was found during $Phase. See $($script:OutputDirectory)."
    }
    Write-Host "$Phase crash check passed."
}

function Wait-SmokeReport {
    param([Parameter(Mandatory = $true)][int]$TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $result = Invoke-Adb @(
            "shell", "run-as", $script:PackageName, "cat", $script:ReportRelativePath
        ) -AllowFailure
        if ($result.ExitCode -eq 0 -and $result.Output -match '(?m)^exit_code=') {
            return $result.Output
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "The hosted Engine smoke report was not completed within $TimeoutSeconds seconds."
}

function Assert-SmokeReport {
    param([Parameter(Mandatory = $true)][string]$Report)

    $values = @{}
    foreach ($line in ($Report -split '\r?\n')) {
        $separator = $line.IndexOf('=')
        if ($separator -ge 0) {
            $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }
    $expected = [ordered]@{
        exit_code = "0"
        had_frame = "1"
        input_sent = "1"
        input_changed = "1"
        action_observed = "1"
        error = ""
    }
    foreach ($entry in $expected.GetEnumerator()) {
        if (-not $values.ContainsKey($entry.Key)) {
            throw "Smoke report is missing '$($entry.Key)':`n$Report"
        }
        if ($values[$entry.Key] -ne $entry.Value) {
            throw "Smoke report '$($entry.Key)' is '$($values[$entry.Key])', expected '$($entry.Value)':`n$Report"
        }
    }
    Write-Host "Hosted Engine smoke report passed all required assertions."
}

function Remove-AppReport {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $null = Invoke-Adb @(
        "shell", "run-as", $script:PackageName, "rm", "-f", $RelativePath
    )
}

function Wait-AppReport {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$CompletionPattern,
        [ValidateRange(1, 120)][int]$TimeoutSeconds = 45
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $result = Invoke-Adb @(
            "shell", "run-as", $script:PackageName, "cat", $RelativePath
        ) -AllowFailure
        if ($result.ExitCode -eq 0 -and $result.Output -match $CompletionPattern) {
            return $result.Output
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "App report '$RelativePath' was not completed within $TimeoutSeconds seconds."
}

function ConvertFrom-KeyValueReport {
    param([Parameter(Mandatory = $true)][string]$Report)

    $values = @{}
    foreach ($line in ($Report -split '\r?\n')) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }
    $values
}

function Assert-KeyValueReport {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Report,
        [Parameter(Mandatory = $true)][hashtable]$Expected
    )

    $values = ConvertFrom-KeyValueReport -Report $Report
    foreach ($key in $Expected.Keys) {
        if (-not $values.ContainsKey($key)) {
            throw "$Name report is missing '$key':`n$Report"
        }
        if ($values[$key] -ne $Expected[$key]) {
            throw "$Name report '$key' is '$($values[$key])', expected '$($Expected[$key])':`n$Report"
        }
    }
    Write-Host "$Name report passed all required assertions."
}

function Wait-LogPattern {
    param(
        [Parameter(Mandatory = $true)][string]$Pattern,
        [ValidateRange(1, 60)][int]$TimeoutSeconds = 20
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $log = (Invoke-Adb @("logcat", "-d", "-v", "threadtime")).Output
        if ($log -match $Pattern) { return $log }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected log pattern was not observed within $TimeoutSeconds seconds: $Pattern"
}

function Test-HostedDialog {
    param(
        [Parameter(Mandatory = $true)][string]$Route,
        [Parameter(Mandatory = $true)][int]$Kind,
        [Parameter(Mandatory = $true)][string]$Phase,
        [string[]]$AdditionalArguments = @(),
        [string]$AdditionalPattern = ""
    )

    Stop-App
    Clear-DeviceLogs
    $arguments = @("--hosted-test-dialog", $Route, "--initial-page", "engine")
    if ($AdditionalArguments.Count -gt 0) { $arguments += $AdditionalArguments }
    Start-App -NativeArguments $arguments
    $commonPattern =
        "CardWirthDialog Android common presentation kind=$Kind size=[1-9][0-9]*x[1-9][0-9]* controls=[1-9][0-9]*"
    $log = Wait-LogPattern -Pattern $commonPattern
    if (-not [string]::IsNullOrWhiteSpace($AdditionalPattern) -and
        $log -notmatch $AdditionalPattern) {
        throw "The $Phase dialog did not satisfy its complete-model contract: $AdditionalPattern"
    }
    $null = Write-TestText -Name "$Phase-logcat.log" -Content ($log + "`n")
    Assert-ActivityRunning -Phase $Phase
    Save-Screenshot -Phase $Phase
    Assert-NoCrash -Phase $Phase
    Write-Host "$Phase uses the common complete dialog model and presentation route."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $androidRoot "app\build\reports\device-test-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$script:OutputDirectory = $OutputDirectory
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

$state = Invoke-Adb @("get-state")
if ($state.Output.Trim() -ne "device") {
    throw "Android device $Serial is not ready: $($state.Output)"
}
$model = (Invoke-Adb @("shell", "getprop", "ro.product.model")).Output.Trim()
$abi = (Invoke-Adb @("shell", "getprop", "ro.product.cpu.abi")).Output.Trim()
Write-Host "Device: $Serial model=$model abi=$abi"

$savedStayAwake = (
    Invoke-Adb @("shell", "settings", "get", "global", "stay_on_while_plugged_in")
).Output.Trim()
try {
Set-GlobalSetting -Name "stay_on_while_plugged_in" -Value "2"
Wake-Device

if (-not $SkipBuild) {
    $env:ANDROID_HOME = $sdkRoot
    $env:ANDROID_SDK_ROOT = $sdkRoot
    Push-Location $androidRoot
    try {
        & .\gradlew.bat assembleAllDebug --console=plain
        if ($LASTEXITCODE -ne 0) {
            throw "Gradle assembleAllDebug failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}
if (-not (Test-Path -LiteralPath $buildApkPath)) {
    throw "ALL Debug APK was not found: $buildApkPath"
}

$null = New-Item -ItemType Directory -Path $distAndroidRoot -Force
Copy-Item -LiteralPath $buildApkPath -Destination $apkPath -Force

$apk = Get-Item -LiteralPath $apkPath
Write-Host "Installing without clearing app data: $($apk.FullName) ($($apk.Length) bytes)"
$install = Invoke-Adb @("install", "-r", "-t", $apk.FullName)
Write-Host $install.Output

Stop-App
Remove-AppReport -RelativePath $reportRelativePath
Clear-DeviceLogs
$smokeArguments = @(
    "--hosted-engine-smoke-test",
    "--hosted-engine-smoke-report",
    $reportAbsolutePath,
    "--initial-page",
    "engine"
)
Start-App -NativeArguments $smokeArguments
$report = Wait-SmokeReport -TimeoutSeconds $SmokeTimeoutSeconds
$null = Write-TestText -Name "hosted-smoke.txt" -Content ($report + "`n")
Assert-SmokeReport -Report $report
Start-Sleep -Milliseconds 750
Assert-NoCrash -Phase "smoke"

Stop-App
Remove-AppReport -RelativePath $layoutReportRelativePath
Clear-DeviceLogs
Start-App -NativeArguments @(
    "--hosted-layout-test",
    "--hosted-layout-report",
    $layoutReportAbsolutePath,
    "--initial-page",
    "engine"
)
$layoutReport = Wait-AppReport `
    -RelativePath $layoutReportRelativePath `
    -CompletionPattern '(?m)^layout_pass=' `
    -TimeoutSeconds $SmokeTimeoutSeconds
$null = Write-TestText -Name "hosted-layout.txt" -Content ($layoutReport + "`n")
Assert-KeyValueReport -Name "Hosted ALL safe-area layout" -Report $layoutReport -Expected @{
    debugger_visible = "1"
    horizontal = "1"
    root_matches_safe = "1"
    alone_fills_root = "1"
    group_fills_root = "1"
    tiled_fills_root = "1"
    vertical_edges_fill = "1"
    outer_padding = "0"
    layout_pass = "1"
    engine_debug_gap = "0.000"
}
Assert-ActivityRunning -Phase "layout"
Save-Screenshot -Phase "layout"
Assert-NoCrash -Phase "layout"

Test-HostedDialog `
    -Route "settings" `
    -Kind 1 `
    -Phase "dialog-settings-full" `
    -AdditionalPattern 'CardWirthDialog Android full common settings: tabs=6 tab=[0-5] fields=[1-9][0-9]* scroll=[0-9]+'
Test-HostedDialog -Route "yado" -Kind 2 -Phase "dialog-yado-full"

Stop-App
Clear-DeviceLogs
Start-App
Start-Sleep -Seconds $LaunchSettleSeconds
Assert-ActivityRunning -Phase "normal"
Assert-UserDataLayout
Assert-LandscapeLock
Save-Screenshot -Phase "normal"
Assert-NoCrash -Phase "normal"

Stop-App
Clear-DeviceLogs
Remove-AppReport -RelativePath $zoomReportRelativePath
Start-App -NativeArguments @(
    "--editor-zoom-test",
    "--editor-zoom-report",
    $zoomReportAbsolutePath,
    "--initial-page",
    "editor"
)
$zoomReport = Wait-AppReport `
    -RelativePath $zoomReportRelativePath `
    -CompletionPattern '(?m)^gesture_complete=' `
    -TimeoutSeconds $SmokeTimeoutSeconds
$null = Write-TestText -Name "editor-zoom.txt" -Content ($zoomReport + "`n")
Assert-KeyValueReport -Name "Editor pinch zoom" -Report $zoomReport -Expected @{
    quantized = "1"
    gesture_complete = "1"
    home_fixed = "1"
    no_accidental_home = "1"
}
Assert-ActivityRunning -Phase "editor"
Save-Screenshot -Phase "editor"
Assert-NoCrash -Phase "editor"

Write-Host "Android ALL device test passed. The editor launch remains active on the device."
Write-Host "Results: $OutputDirectory"
} finally {
    Set-GlobalSetting -Name "stay_on_while_plugged_in" -Value $savedStayAwake
}
