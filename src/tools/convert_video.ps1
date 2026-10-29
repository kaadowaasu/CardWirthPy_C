param(
    [string]$Ffmpeg = "ffmpeg"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$inputFile = Join-Path $projectRoot "test_resource\image\MapOfWirth_animated.webm"
$outputFile = Join-Path $projectRoot "test_resource\image\MapOfWirth_animated.webp"

if (-not (Test-Path -LiteralPath $inputFile)) {
    throw "Input video not found: $inputFile"
}

& $Ffmpeg -y -v warning -i $inputFile -an -c:v libwebp_anim `
    -lossless 0 -q:v 82 -compression_level 4 -loop 0 $outputFile
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed with exit code $LASTEXITCODE"
}

Write-Output "Created runtime animation asset: $outputFile"
