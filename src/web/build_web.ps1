[CmdletBinding()]
param(
    [ValidateSet("Engine", "All")]
    [string]$Variant = "All",
    [ValidateSet("Release", "Debug", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [bool]$SkipFinalOptimization = $false,
    [switch]$IoServer = $true,
    [string]$EmsdkRoot = "",
    [int]$Parallelism = 0,
    [switch]$Clean,
    [switch]$PackageOnly,
    [string]$BuildDirectory = "",
    [switch]$Serve,
    [int]$Port = 8000
)

$ErrorActionPreference = "Stop"
$webRoot = $PSScriptRoot
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $webRoot "..")).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $sourceRoot "..")).Path
$variantId = $Variant.ToLowerInvariant()
$ioModelSuffix = if ($IoServer) { "-io-server" } else { "" }
$buildRoot = Join-Path $repoRoot ("build/web-" + $variantId + "-" + $Configuration.ToLowerInvariant() + "-pthread" + $ioModelSuffix)
if ($PackageOnly -and $Clean) { throw '-PackageOnly cannot be combined with -Clean.' }
if ($BuildDirectory) {
    $buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $allowedBuildRoot = (Join-Path $repoRoot 'build') + [IO.Path]::DirectorySeparatorChar
    if (-not $buildRoot.StartsWith($allowedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "BuildDirectory must be under the repository build directory: $buildRoot"
    }
}
$distRoot = Join-Path $buildRoot "dist"
if (-not $PackageOnly) {
$cmake = $null
# Windows のアプリ実行エイリアスより実行ファイルを優先する。エイリアスは PowerShell
# で見つかっても、emcmake の子の Python から起動できない。
foreach ($candidate in @(
    "$env:LOCALAPPDATA\Android\Sdk\cmake\3.31.6\bin\cmake.exe",
    "$env:LOCALAPPDATA\Android\Sdk\cmake\3.22.1\bin\cmake.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")) {
    if (Test-Path -LiteralPath $candidate) { $cmake = Get-Item -LiteralPath $candidate; break }
}
if (-not $cmake) {
    $candidate = Get-Command cmake -ErrorAction SilentlyContinue
    if ($candidate -and $candidate.Source -notmatch '[\\/]WindowsApps[\\/]') {
        $cmake = $candidate
    }
}
if (-not $cmake) { throw "CMake was not found." }
$cmakeCommand = if ($cmake.Source) { $cmake.Source } else { $cmake.FullName }

function Import-EmsdkEnvironment([string]$Root) {
    $candidates = @()
    if ($Root) { $candidates += $Root }
    if ($env:EMSDK) { $candidates += $env:EMSDK }
    $candidates += @(
        (Join-Path $repoRoot "build\emsdk"),
        "D:\tools\emsdk",
        (Join-Path $env:USERPROFILE "emsdk"))
    foreach ($candidate in $candidates) {
        $environmentScript = Join-Path $candidate "emsdk_env.bat"
        if (-not (Test-Path -LiteralPath $environmentScript)) { continue }
        $importedPath = $null
        cmd.exe /d /c "call `"$environmentScript`" >nul && set" | ForEach-Object {
            if ($_ -notmatch '^([^=]+)=(.*)$') { return }
            $name = $matches[1]
            $value = $matches[2]
            # Windows の起動元によっては Path と PATH の
            # 両方がある。emsdk は大文字側を更新するため、子の cmd
            # の環境を取り込むとき、古い混在表記の項目で上書きしない。
            if ($name -ieq "PATH") {
                if ($name -ceq "PATH") { $importedPath = $value }
            } else {
                Set-Item -Path ("Env:" + $name) -Value $value
            }
        }
        if ($importedPath) { $env:PATH = $importedPath }
        return
    }
}

if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
    Import-EmsdkEnvironment $EmsdkRoot
}
$emcmake = Get-Command emcmake -ErrorAction SilentlyContinue
if (-not $emcmake) { $emcmake = Get-Command emcmake.bat -ErrorAction SilentlyContinue }
if (-not $emcmake -and $EmsdkRoot) {
    $directEmcmake = Join-Path $EmsdkRoot "upstream\emscripten\emcmake.exe"
    if (Test-Path -LiteralPath $directEmcmake) { $emcmake = Get-Item -LiteralPath $directEmcmake }
}
if (-not $emcmake) {
    throw "Emscripten SDK was not found. Run emsdk_env.bat or pass -EmsdkRoot."
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "Ninja was not found."
}
$ninjaCommand = (Get-Command ninja -ErrorAction Stop).Source
$env:PATH = ([System.IO.Path]::GetDirectoryName($cmakeCommand)) + ";" +
            ([System.IO.Path]::GetDirectoryName($ninjaCommand)) + ";" + $env:PATH
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "Python was not found."
}

# 統合 Engine+Editor の Asyncify は、制限し
# ないと全論理 CPU に並列化し、最後の Binaryen 処理で
# メモリーを使い過ぎる。I/O サーバー方式はこの変換を行わない。
if (-not $IoServer -and -not $env:BINARYEN_CORES) { $env:BINARYEN_CORES = "2" }

if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
    if (-not $resolvedBuild.StartsWith((Join-Path $repoRoot ("build\web-" + $variantId + "-")), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing an unexpected clean path: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$finalOptimizationFlag = if ($SkipFinalOptimization) { "ON" } else { "OFF" }
$ioServerFlag = if ($IoServer) { "ON" } else { "OFF" }
$emcmakeCommand = if ($emcmake.Source) { $emcmake.Source } else { $emcmake.FullName }
& $emcmakeCommand $cmakeCommand -S $sourceRoot -B $buildRoot -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCARDWIRTH_APP_VARIANT=$Variant" `
    "-DCARDWIRTH_SUPPRESS_WARNINGS=ON" `
    "-DCARDWIRTH_WEB_PTHREADS=ON" `
    "-DCARDWIRTH_WEB_IO_SERVER=$ioServerFlag" `
    "-DCARDWIRTH_WEB_SKIP_FINAL_OPTIMIZATION=$finalOptimizationFlag" `
    "-DBUILD_TESTING=OFF"
if ($LASTEXITCODE -ne 0) { throw "Web CMake configure failed: $LASTEXITCODE" }

$buildArgs = @("--build", $buildRoot, "--config", $Configuration)
if ($Parallelism -gt 0) { $buildArgs += @("--parallel", $Parallelism) }
& $cmakeCommand @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Web build failed: $LASTEXITCODE" }

# Emscripten のアプリビルドでは、ネイティブ C++ テストは
# Web 成果物に含めないため BUILD_TESTING は OFF の
# ままにする。選択画面連携は軽い Node 検査なので、通常の Web ビ
# ルドで明示的に実行し、配布する JavaScript の検査を省かない。
$webContractTests = @(
    "web_document_cancel_tests.cjs",
    "web_fonts_tests.cjs",
    "web_editor_save_contract_tests.cjs",
    "web_all_editor_contract_tests.cjs"
)
$webTestNode = Get-Command node -ErrorAction SilentlyContinue
if (-not $webTestNode -and $env:EMSDK_NODE -and (Test-Path -LiteralPath $env:EMSDK_NODE)) {
    $webTestNode = Get-Item -LiteralPath $env:EMSDK_NODE
}
if (-not $webTestNode) { throw "Node.js is required for Web ALL contract tests." }
$webTestNodeCommand = if ($webTestNode.Source) { $webTestNode.Source } else { $webTestNode.FullName }
foreach ($webContractTestName in $webContractTests) {
    $webContractTest = Join-Path (Join-Path $sourceRoot "tests") $webContractTestName
    & $webTestNodeCommand $webContractTest
    if ($LASTEXITCODE -ne 0) { throw "Web contract test failed ($webContractTestName): $LASTEXITCODE" }
}
}

# 検証済みの Asyncify 制限付きビルドの梱
# 包で、再リンクを起こさない。構成が違う、またはビ
# ルドが不完全なら、配布先を変える前に失敗する。
$artifactBase = if ($Variant -eq "Engine") { "CardWirthPy_C" } else { "CardWirthPy_C_All" }
$cacheText = [IO.File]::ReadAllText((Join-Path $buildRoot 'CMakeCache.txt'))
if ($cacheText -notmatch "(?m)^CARDWIRTH_APP_VARIANT:STRING=$Variant\r?$" -or
    $cacheText -notmatch '(?m)^CARDWIRTH_WEB_PTHREADS:BOOL=ON\r?$') {
    throw "Expected a pthread $Variant build: $buildRoot"
}
foreach ($extension in @('js', 'wasm')) {
    $requiredArtifact = Join-Path $buildRoot ($artifactBase + '.' + $extension)
    if (-not (Test-Path -LiteralPath $requiredArtifact -PathType Leaf) -or
        (Get-Item -LiteralPath $requiredArtifact).Length -eq 0) {
        throw "Missing completed Web artifact: $requiredArtifact"
    }
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$libraryRoot = Join-Path $distRoot "lib"
New-Item -ItemType Directory -Path $libraryRoot -Force | Out-Null
# パッケージ更新前に、旧 Web の平坦な配置からプログラム所有の項目を除く
# 。この生成 dist ディレクトリーには UserData を置かない。
foreach ($legacyName in @("CardWirthC_All.wasm", "CardWirthC_All.worker.js", "CardWirthC_All.data", "vendor")) {
    $legacyPath = Join-Path $distRoot $legacyName
    if (Test-Path -LiteralPath $legacyPath) { Remove-Item -LiteralPath $legacyPath -Recurse -Force }
}
Copy-Item -LiteralPath (Join-Path $webRoot "index.html") -Destination $distRoot -Force
$distIndex = Join-Path $distRoot "index.html"
$indexText = [System.IO.File]::ReadAllText($distIndex)
$indexText = $indexText.Replace('data-cardwirth-pthreads="0"', 'data-cardwirth-pthreads="1"')
$artifactBase = if ($Variant -eq "Engine") { "CardWirthPy_C" } else { "CardWirthPy_C_All" }
$indexText = $indexText.Replace('data-cardwirth-variant="All"', ('data-cardwirth-variant="' + $Variant + '"'))
$indexText = $indexText.Replace('src="CardWirthPy_C_All.js', ('src="' + $artifactBase + '.js'))
[System.IO.File]::WriteAllText(
    $distIndex, $indexText, [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repoRoot "README.md") -Destination (Join-Path $buildRoot "README.md") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE.md") -Destination (Join-Path $buildRoot "LICENSE.md") -Force
Copy-Item -LiteralPath (Join-Path $webRoot "app.css") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "web_storage.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "web_storage_picker.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "wasm_parts.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "download_packages.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "vfs_service_worker.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "web_fonts.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "web_storage_selftest.html") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "web_storage_selftest.js") -Destination $distRoot -Force
Copy-Item -LiteralPath (Join-Path $webRoot "vendor") -Destination $libraryRoot -Recurse -Force
$fontSource = Join-Path $webRoot "DataLoader/app/Data/Font"
$fontDestination = Join-Path $libraryRoot "fonts"
if (Test-Path -LiteralPath $fontDestination) {
    Remove-Item -LiteralPath $fontDestination -Recurse -Force
}
New-Item -ItemType Directory -Path $fontDestination -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $fontSource "NotoSansCJKjp-Regular.otf") -Destination $fontDestination -Force
Copy-Item -LiteralPath (Join-Path $fontSource "OFL.txt") -Destination $fontDestination -Force
Copy-Item -LiteralPath (Join-Path $fontSource "IPA") -Destination (Join-Path $fontDestination "IPA") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $webRoot "font_manifest.json") -Destination (Join-Path $libraryRoot "font-manifest.json") -Force
$legacyDataLoader = Join-Path $distRoot "DataLoader"
if (Test-Path -LiteralPath $legacyDataLoader) {
    Remove-Item -LiteralPath $legacyDataLoader -Recurse -Force
}

Get-ChildItem -LiteralPath $buildRoot -Filter ($artifactBase + "*.js") -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*.worker.js" } |
    Copy-Item -Destination $distRoot -Force
Get-ChildItem -LiteralPath $buildRoot -Filter ($artifactBase + "*.data") -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination $distRoot -Force
foreach ($extension in @("wasm", "worker.js")) {
    Get-ChildItem -LiteralPath $buildRoot -Filter ($artifactBase + "*." + $extension) -File -ErrorAction SilentlyContinue |
        Copy-Item -Destination $libraryRoot -Force
}
$asyncModel = if ($IoServer) { "I/O server" } else { "Asyncify" }
Write-Host "Web $Variant ${Configuration} ($asyncModel): $distRoot"
if ($Serve) { python (Join-Path $webRoot "serve.py") --root $distRoot --port $Port --open }
