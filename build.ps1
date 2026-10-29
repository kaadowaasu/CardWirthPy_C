[CmdletBinding()]
param(
    [ValidateRange(1, 10)]
    [int]$Parallelism = 10,
    [ValidateSet('Debug', 'Release', 'MinSizeRel', 'RelWithDebInfo')]
    [string]$Configuration = 'MinSizeRel',
    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',
    [ValidateSet('Engine', 'OneWindow', 'Editor', 'Explorer', 'All')]
    [string]$Variant = 'Engine',
    [string]$Target = '',
    [string]$BuildDirectory = '',
    [string]$TestEnvironmentDirectory = '',
    [switch]$Rebuild,
    [switch]$Reconfigure,
    [switch]$BuildTests,
    [switch]$RunTests,
    [switch]$SkipDistributionCopy = $true,
    [switch]$AuditVirtual,
    [switch]$ShowWarnings
)

$ErrorActionPreference = 'Stop'

function ConvertTo-NativeArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Set-CleanProcessEnvironment {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.ProcessStartInfo]$StartInfo,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    # ProcessStartInfo がデスクトップのホストから Path と PATH
    # の両方を継承する場合がある。Windows は同一名と扱うが、一部の MSBuild
    # タスクは大小文字を区別する辞書へ複製し、CL.exe の起動前に失敗する。
    $sourceEnvironment = [System.Environment]::GetEnvironmentVariables()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    # 旧形式の大小文字を区別しない StringDictionary も使う。MSBuild
    # の VC タスクは ProcessStartInfo.EnvironmentVariables
    # を読むため、新しい Environment 辞書だけを消去して
    # も、旧取得口に重複した Path/PATH が残る場合がある。
    $StartInfo.EnvironmentVariables.Clear()
    foreach ($entry in $sourceEnvironment.GetEnumerator()) {
        $name = [string]$entry.Key
        if ($name -ieq 'PATH' -or -not $seen.Add($name)) { continue }
        $StartInfo.EnvironmentVariables[$name] = [string]$entry.Value
    }
    # MSBuild の VC タスクは CL 起動前に大
    # 文字の PATH を追加する。Path を渡すと大小
    # 文字を区別する中間表に両方が入り、MSB6001 で
    # 失敗するため、VC タスクと同じ正式な表記を使う。
    $StartInfo.EnvironmentVariables['PATH'] = $PathValue
}

function Invoke-CleanCommand {
    param(
        [Parameter(Mandatory = $true)][string]$CommandLine,
        [Parameter(Mandatory = $true)][string]$PathValue
    )

    # 重複を明示的に除いた環境で cmd.exe を起動する
    # 。親に Path/PATH が両方ある場合、$env:Path
    # の代入だけでは不十分。cmd.exe は最初の大
    # 小文字表記を保持するため、ここでどちらの set
    # コマンドも実行せず、除いた重複を再作成しない。
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = if ($env:ComSpec) { $env:ComSpec } else { 'C:\Windows\System32\cmd.exe' }
    # /c は最初の引用トークンをコマンドとして扱い
    # 、引用符を外す。空白を含む実行パスが一つのトー
    # クンに残るよう、全コマンドの先頭に追加の引用
    # 符を置く。実行パス自体は引用済みなので、cmd.exe
    # /c 用の先頭だけを足す。末尾にも足す
    # と最後の -D 値に引用符が混入する。
    $startInfo.Arguments = '/d /s /c "' + $CommandLine
    $startInfo.UseShellExecute = $false
    Set-CleanProcessEnvironment -StartInfo $startInfo -PathValue $PathValue
    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $process.WaitForExit()
        return $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

$sourceDirectory = Join-Path $PSScriptRoot 'src'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildSuffix = if ($Variant -eq 'Engine') { '' } else { '-' + $Variant }
    $BuildDirectory = Join-Path $PSScriptRoot ('build\windows-433' + $buildSuffix)
}
$sourceDirectory = [System.IO.Path]::GetFullPath($sourceDirectory)
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$testsEnabled = $BuildTests -or $RunTests

# 製品ビルドで動作検査の実行ファイルをコンパイル・リンクする必要はな
# い。明示的に検査する場合は -BuildTests または -RunTests
# を使う。検査ターゲットは独立した CMake 実行ファイ
# ルとし、cardwirth_sample へリンクしない。
$testingValue = if ($testsEnabled) { 'ON' } else { 'OFF' }
$warningsEnabled = $ShowWarnings -or $AuditVirtual
$warningSuppressionValue = if ($warningsEnabled) { 'OFF' } else { 'ON' }

# SDL と同梱依存も同じプロジェクトで設定するため、
# CMake の再生成は比較的重い。通常の差分ビルドは
# MSBuild が .cpp/.hpp の依存を追
# 跡するので不要。入力変更、キャッシュ欠落、明示的な
# -Reconfigure の場合だけ再実行する。
$cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
$configureMarkerPath = Join-Path $BuildDirectory 'CMakeFiles\generate.stamp'
$configureMarker = if (Test-Path -LiteralPath $configureMarkerPath -PathType Leaf) {
    Get-Item -LiteralPath $configureMarkerPath
} elseif (Test-Path -LiteralPath $cachePath -PathType Leaf) {
    Get-Item -LiteralPath $cachePath
} else {
    $null
}
$configureRequired = $Reconfigure -or $null -eq $configureMarker
if (-not $configureRequired -and (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    $cachedPlatform = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_GENERATOR_PLATFORM:INTERNAL=(.*)$' |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    if ($cachedPlatform -and $cachedPlatform -ne $Platform) {
        $configureRequired = $true
    }
    $cachedVariant = Select-String -LiteralPath $cachePath -Pattern '^CARDWIRTH_APP_VARIANT:STRING=(.*)$' |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    # CARDWIRTH_APP_VARIANT 導入前のビルド領域も古い設定と
    # して扱う。項目がないと、-Variant Editor/All を指定して
    # も CMake を呼ばず旧 Engine プロジェクトを再利用してしまう。
    if (-not $cachedVariant -or $cachedVariant -ne $Variant) {
        $configureRequired = $true
    }
    $cachedTesting = Select-String -LiteralPath $cachePath -Pattern '^BUILD_TESTING:BOOL=(.*)$' |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    # 古いビルドツリーで BUILD_TESTING が
    # 明示されていない場合も再設定する。既定の製品ビルド
    # を一定にし、検査プロジェクトは要求時だけ生成する。
    if (-not $cachedTesting -or $cachedTesting -ne $testingValue) {
        $configureRequired = $true
    }
    $cachedWarningSuppression = Select-String -LiteralPath $cachePath -Pattern '^CARDWIRTH_SUPPRESS_WARNINGS:BOOL=(.*)$' |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    if (-not $cachedWarningSuppression -or $cachedWarningSuppression -ne $warningSuppressionValue) {
        $configureRequired = $true
    }
}
if (-not $configureRequired) {
    # このスクリプトは Windows の Visual Studio プロジェク
    # トだけを生成する。Android の CMake 生成物は platform\android\app\.cxx
    # にあり、Windows 用の入力に含めると Android
    # のビルドごとに不要な Windows 再設定が発生する。
    $androidSourcePrefix = [System.IO.Path]::GetFullPath(
        (Join-Path $sourceDirectory 'platform\android')
    ).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    $cmakeInputs = Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File -ErrorAction Stop |
        Where-Object {
            -not $_.FullName.StartsWith($androidSourcePrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
            (
                $_.Name -eq 'CMakeLists.txt' -or
                $_.Name -eq 'CMakePresets.json' -or
                $_.Extension -eq '.cmake' -or
                $_.Name.EndsWith('.cmake.in', [System.StringComparison]::OrdinalIgnoreCase)
            )
        }
    $configureRequired = $null -ne ($cmakeInputs | Where-Object { $_.LastWriteTimeUtc -gt $configureMarker.LastWriteTimeUtc } | Select-Object -First 1)
}

# 必要なときだけ設定し、変更のない呼び出しを高速な差分 MSBuild
# に保つ。-Reconfigure は -Rebuild と分け、オ
# ブジェクトを消さず CMake オプションを変えられるようにする。
$cmakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
# Visual Studio 用の生成器は、選択した VS ツールチェーンに同梱
# CMake があればそれを使う。無関係な PATH 上の CMake を先
# に選ぶと、既存の VS ビルド領域に非互換のコンパイラー識別状態が残り得る。
$cmakePath = $cmakeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $cmakePath) {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmake) { throw 'cmake.exe was not found. Install Visual Studio C++ CMake tools or add CMake to Path.' }
    $cmakePath = $cmake.Source
}

$normalizedPath = [string]$env:Path
if ($configureRequired) {
    Write-Host "Configuring $sourceDirectory -> $BuildDirectory ($Platform)"
    $variantArgument = '-D"CARDWIRTH_APP_VARIANT=' + $Variant + '"'
    $testingArgument = '-D"BUILD_TESTING=' + $testingValue + '"'
    $warningArgument = '-D"CARDWIRTH_SUPPRESS_WARNINGS=' + $warningSuppressionValue + '"'
    $configureArgumentList = @()
    if (-not $warningsEnabled) {
        # CMake の状態・警告出力を抑え、設定エラーは表示す
        # る。コンソール出力だけを変え、設定の動作は変えない。
        $configureArgumentList += '--log-level=ERROR'
        $configureArgumentList += '-Wno-dev'
    }
    $configureArgumentList += @('-S', $sourceDirectory, '-B', $BuildDirectory, '-G', 'Visual Studio 17 2022', '-A', $Platform,
        $variantArgument, $testingArgument, $warningArgument)
    $configureArguments = ($configureArgumentList |
        ForEach-Object {
            if ($_ -like '-D*CARDWIRTH_APP_VARIANT=*' -or
                $_ -like '-D*BUILD_TESTING=*' -or
                $_ -like '-D*CARDWIRTH_SUPPRESS_WARNINGS=*') { $_ } else { ConvertTo-NativeArgument $_ }
        }) -join ' '
    Write-Host "CMake arguments: $configureArguments"
    $configureExitCode = Invoke-CleanCommand -CommandLine ('"' + $cmakePath + '" ' + $configureArguments) -PathValue $normalizedPath
    if ($configureExitCode -ne 0) {
        throw "CMake configure failed with exit code $configureExitCode"
    }
} else {
    Write-Host "Skipping CMake configure (inputs unchanged; use -Reconfigure to force it)."
}

$projectPath = Join-Path $BuildDirectory 'cardwirth_sample.vcxproj'
if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Visual Studio project was not found: $projectPath"
}
$buildProjectPath = $projectPath
if ($testsEnabled) {
    # CMake のプロジェクト名は CardWirthPy_C。旧名は既存ビルドツ
    # リー用だけに残し、検査ターゲットのビルドは実際に生成したソリューションを使う。
    $solutionPath = Join-Path $BuildDirectory 'CardWirthPy_C.sln'
    if (-not (Test-Path -LiteralPath $solutionPath -PathType Leaf)) {
        $solutionPath = Join-Path $BuildDirectory 'CardWirthC.sln'
    }
    if (-not (Test-Path -LiteralPath $solutionPath -PathType Leaf)) {
        throw "Test-enabled solution was not found: $solutionPath"
    }
    $buildProjectPath = if ([string]::IsNullOrWhiteSpace($Target)) {
        $solutionPath
    } else {
        $candidate = Join-Path $BuildDirectory ($Target + '.vcxproj')
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Requested test target was not found: $candidate"
        }
        $candidate
    }
}

# プロジェクトとコンパイラーの並列数をそろえる。既定は
# 10 で、-Parallelism で変更できる。
$msbuild = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild) {
    $vsCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
    )
    $msbuildPath = $vsCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $msbuildPath) { throw 'MSBuild.exe was not found. Start a Visual Studio Developer PowerShell or install MSBuild.' }
} else {
    $msbuildPath = $msbuild.Source
}

Write-Host "Building $buildProjectPath ($Configuration/$Platform, parallelism=$Parallelism)"
$existingCl = if ($env:CL) { $env:CL } else { '' }
if ($AuditVirtual) {
    # C4263/C4264 はシグネチャー不一致で基底の仮想関数を上
    # 書きせず隠す警告。C4265 は多態クラスの非仮想デストラクタ
    # ー、C4266 は派生側の仮想オーバーロードによる基底の隠蔽。
    $existingCl = ($existingCl + ' /w14263 /w14264 /w14265 /w14266').Trim()
}
$buildTarget = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
$msbuildArguments = @(
    $buildProjectPath,
    $buildTarget,
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/m:$Parallelism",
    '/nr:false',
    "/p:CL_MPCount=$Parallelism",
    '/p:UseMultiToolTask=true',
    '/verbosity:minimal'
)
# 通常ビルドは全ツールチェーンで CMake 側からコンパイラー警告を
# 抑える。ErrorsOnly は MSBuild のプロジェクト・
# タスク警告への追加境界。-ShowWarnings は全診断を表示
# する。-AuditVirtual も警告を表示し、上で有効にした四
# つの警告がログ処理で捨てられて誤って問題なしにならないようにする。
if (-not $warningsEnabled) {
    $msbuildArguments += '/consoleloggerparameters:ErrorsOnly'
}
$msbuildArguments = ($msbuildArguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' '
$buildExitCode = Invoke-CleanCommand -CommandLine ('set "CL_MPCount=' + $Parallelism + '" & set "CL=' +
    $existingCl.Replace('"', '') + '" & "' + $msbuildPath + '" ' + $msbuildArguments) -PathValue $normalizedPath
if ($buildExitCode -ne 0) {
    throw "MSBuild failed with exit code $buildExitCode"
}

if ($RunTests) {
    $ctest = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if ($ctest) {
        $ctestPath = $ctest.Source
    } else {
        $cmakeDirectory = Split-Path -Parent $cmakePath
        $ctestPath = Join-Path $cmakeDirectory 'ctest.exe'
        if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
            throw 'ctest.exe was not found. Install the CMake tools to run the requested tests.'
        }
    }
    $ctestArguments = @(
        '--test-dir', $BuildDirectory,
        '-C', $Configuration,
        '--output-on-failure'
    )
    $ctestCommand = ('"' + $ctestPath + '" ' +
        (($ctestArguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' '))
    Write-Host "Running CTest ($Configuration)"
    $ctestExitCode = Invoke-CleanCommand -CommandLine $ctestCommand -PathValue $normalizedPath
    if ($ctestExitCode -ne 0) {
        throw "CTest failed with exit code $ctestExitCode"
    }
}

# ビルド成功後は専用スクリプトで手動検査用の配布先へコピ
# ーする。コピーだけを独立して再実行できるようにする。
if ($SkipDistributionCopy) {
    Write-Host 'Skipping distribution copy (-SkipDistributionCopy).'
    return
}
$copyScript = Join-Path $PSScriptRoot 'copy_to_test_env.ps1'
if (-not (Test-Path -LiteralPath $copyScript -PathType Leaf)) {
    throw "Copy script was not found: $copyScript"
}
$copyArguments = @{
    BuildDirectory = $BuildDirectory
    Configuration = $Configuration
    Platform = $Platform
    Variant = $Variant
    CopyTestBinaries = $testsEnabled
}
if (-not [string]::IsNullOrWhiteSpace($TestEnvironmentDirectory)) {
    $copyArguments['TargetDirectory'] = $TestEnvironmentDirectory
}
& $copyScript @copyArguments
