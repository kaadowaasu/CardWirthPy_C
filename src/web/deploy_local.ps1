param(
    [string]$BuildDirectory = "",
    [string]$Destination = "",
    [int]$Port = 8121,
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repoRoot "build/web-all-release-pthread-io-server/dist" }
if (!$Version) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $BuildDirectory "lib/CardWirthPy_C_All.wasm") -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
    $Version = (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + $hash
}
& (Join-Path $PSScriptRoot "package_release.ps1") -BuildDirectory $BuildDirectory -Destination $Destination -Version $Version
if (!$?) { throw "Web packaging failed" }
Write-Output "Local URL: http://127.0.0.1:$Port/"
