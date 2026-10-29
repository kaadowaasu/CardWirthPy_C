param(
    [string]$BuildDirectory = "",
    [string]$Destination = "",
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version
)

$ErrorActionPreference = "Stop"
$webSource = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $webSource "../..")).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repoRoot "build/web-all-release-pthread-io-server/dist" }
if (!$Destination) { $Destination = Join-Path $repoRoot "dist/web/ioserver/py_c" }
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$buildHtml = Get-Content -LiteralPath (Join-Path $buildRoot "index.html") -Raw
$threadMode = [regex]::Match($buildHtml, 'data-cardwirth-pthreads="([01])"').Groups[1].Value
if (!$threadMode) { throw "Cannot determine worker mode from build index.html" }
foreach ($name in @("CardWirthPy_C_All.js", "lib/CardWirthPy_C_All.wasm")) {
    if (!(Test-Path -LiteralPath (Join-Path $buildRoot $name) -PathType Leaf)) { throw "Missing build artifact: $name" }
}
foreach ($name in @("lib/font-manifest.json", "lib/fonts")) {
    if (!(Test-Path -LiteralPath (Join-Path $buildRoot $name))) { throw "Missing Web font asset: $name" }
}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$destinationRoot = (Resolve-Path -LiteralPath $Destination).Path
$versionRoot = Join-Path $destinationRoot $Version
if (Test-Path -LiteralPath $versionRoot) { throw "Version already exists; choose a new version: $Version" }
New-Item -ItemType Directory -Path $versionRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot "CardWirthPy_C_All.js") -Destination $versionRoot
$libRoot = Join-Path $versionRoot "lib"
New-Item -ItemType Directory -Path $libRoot | Out-Null
Get-ChildItem -LiteralPath (Join-Path $buildRoot "lib") | Where-Object { $_.Name -ne "CardWirthPy_C_All.wasm" } |
    Copy-Item -Destination $libRoot -Recurse
& node (Join-Path $webSource "split_wasm.cjs") (Join-Path $buildRoot "lib/CardWirthPy_C_All.wasm") $libRoot
if ($LASTEXITCODE -ne 0) { throw "WASM splitting failed" }
foreach ($name in @("app.css", "web_storage.js", "web_storage_picker.js", "wasm_parts.js", "download_packages.js", "vfs_service_worker.js", "web_fonts.js")) {
    Copy-Item -LiteralPath (Join-Path $webSource $name) -Destination $versionRoot
}
$html = [IO.File]::ReadAllText((Join-Path $webSource "index.html"), [Text.Encoding]::UTF8)
$html = $html.Replace('data-cardwirth-pthreads="0"', ('data-cardwirth-pthreads="' + $threadMode + '"'))
$html = $html.Replace('20260904-web-v14', $Version)
$html = $html.Replace('name="cardwirth-wasm-manifest" content=""', ('name="cardwirth-wasm-manifest" content="lib/wasm-parts.json?v=' + $Version + '"'))
[IO.File]::WriteAllText((Join-Path $versionRoot "index.html"), $html, [Text.UTF8Encoding]::new($false))
$readmeSource = Join-Path (Split-Path -Parent $buildRoot) "README.md"
if (!(Test-Path -LiteralPath $readmeSource -PathType Leaf)) {
    $readmeSource = Join-Path $buildRoot "README.md"
}
if (!(Test-Path -LiteralPath $readmeSource -PathType Leaf)) {
    $readmeSource = Join-Path $repoRoot "README.md"
}
if (!(Test-Path -LiteralPath $readmeSource -PathType Leaf)) {
    throw "README.md was not found in the build or repository root."
}
# 共通 README はバージョンフォルダーの隣に一つ置く。各版の起
# 動ページは ../README.md を参照し、本文を複製しない。
Copy-Item -LiteralPath $readmeSource -Destination (Join-Path $destinationRoot "README.md") -Force
$licenseSource = Join-Path (Split-Path -Parent $readmeSource) "LICENSE.md"
if (!(Test-Path -LiteralPath $licenseSource -PathType Leaf)) {
    $licenseSource = Join-Path $repoRoot "LICENSE.md"
}
if (!(Test-Path -LiteralPath $licenseSource -PathType Leaf)) {
    throw "LICENSE.md was not found in the build or repository root."
}
Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $destinationRoot "LICENSE.md") -Force
$hashes = foreach ($name in @("CardWirthPy_C_All.js", "lib/wasm-parts.json", "lib/font-manifest.json")) {
    $hash = Get-FileHash -LiteralPath (Join-Path $versionRoot $name) -Algorithm SHA256
    [ordered]@{ file = $name; sha256 = $hash.Hash }
}
$manifest = [ordered]@{ version = $Version; builtNow = $false; artifacts = @($hashes) } | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $versionRoot "release.json"), $manifest, [Text.UTF8Encoding]::new($false))

$downloadRoot = Join-Path $destinationRoot "DownloadFiles"
New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
if (Test-Path -LiteralPath (Join-Path $downloadRoot "Data.zip")) {
    & node (Join-Path $webSource "create_download_manifest.cjs") $downloadRoot
    if ($LASTEXITCODE -ne 0) { throw "Download manifest generation failed" }
}
$serverRoot = $destinationRoot
if ((Split-Path -Leaf $destinationRoot) -eq "py_c") {
    $serverRoot = Split-Path -Parent $destinationRoot
    # py_c パッケージは既存サイトの下に配置される場
    # 合がある。そのサイトの入口をゲームへの転送に置き換
    # えない。単独のローカル配布でサーバールートに index
    # がない場合だけ、小さな転送ページを置く。
    $serverIndex = Join-Path $serverRoot "index.html"
    if (!(Test-Path -LiteralPath $serverIndex -PathType Leaf)) {
        $redirect = '<!doctype html><html lang="ja"><head><meta charset="utf-8"><script>location.replace("./py_c/" + location.search + location.hash);</script></head><body></body></html>'
        [IO.File]::WriteAllText($serverIndex, $redirect, [Text.UTF8Encoding]::new($false))
    }
}
# ローカル用ツールはサーバールートだけに置き、リリースの部分木には含めない。
foreach ($name in @("serve.py", "start_local_server.ps1", "start_web_chrome.cmd")) {
    Copy-Item -LiteralPath (Join-Path $webSource $name) -Destination $serverRoot -Force
}
Copy-Item -LiteralPath (Join-Path $webSource "RELEASE_LAYOUT_JA.md") -Destination (Join-Path $serverRoot "配布構成.md") -Force
Copy-Item -LiteralPath (Join-Path $webSource "pages_headers") -Destination (Join-Path $serverRoot "_headers") -Force
# ルートの起動ページだけを扱う。各不変バージョンが自身の JS
# ・Wasm URL を持つ。転送前に一フレーム表示され得る
# ため、転送ページは見た目を空に保つ。不正な符号化で残った古
# いファイルの代替ラベルが文字化けして見えることを避ける。
$launcher = '<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CardWirthPy_C</title><script>location.replace("./' + $Version + '/" + location.search + location.hash);</script></head><body></body></html>'
[IO.File]::WriteAllText((Join-Path $destinationRoot "index.html"), $launcher, [Text.UTF8Encoding]::new($false))
Write-Output "Packaged existing build (no compilation): $versionRoot"
Write-Output "Shared downloads: $downloadRoot"
