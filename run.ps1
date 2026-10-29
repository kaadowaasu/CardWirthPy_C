[CmdletBinding()]
param(
    [ValidateRange(1, 10)]
    [int]$Parallelism = 10,
    [ValidateSet('Debug', 'Release', 'MinSizeRel', 'RelWithDebInfo')]
    [string]$Configuration = 'MinSizeRel',
    [ValidateSet('x64', 'Win32')]
    [string]$Platform = 'x64',
    [ValidateSet('Engine', 'Editor', 'Explorer', 'All')]
    [string]$Variant = 'Engine',
    [string]$BuildDirectory = '',
    [string]$TestEnvironmentDirectory = '',
    [switch]$Rebuild,
    [switch]$NoBuild,
    [switch]$Wait,
    [string[]]$ApplicationArguments = @()
)

$ErrorActionPreference = 'Stop'

$buildScript = Join-Path $PSScriptRoot 'build.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Build script was not found: $buildScript"
}

if ([string]::IsNullOrWhiteSpace($TestEnvironmentDirectory)) {
    $TestEnvironmentDirectory = Join-Path $PSScriptRoot 'test_env'
}
$TestEnvironmentDirectory = [System.IO.Path]::GetFullPath($TestEnvironmentDirectory)

if (-not $NoBuild) {
    $buildArguments = @{
        Parallelism = $Parallelism
        Configuration = $Configuration
        Platform = $Platform
        Variant = $Variant
        TestEnvironmentDirectory = $TestEnvironmentDirectory
    }
    if (-not [string]::IsNullOrWhiteSpace($BuildDirectory)) {
        $buildArguments['BuildDirectory'] = $BuildDirectory
    }
    if ($Rebuild) {
        $buildArguments['Rebuild'] = $true
    }
    & $buildScript @buildArguments
}

$executableBaseName = switch ($Variant) {
    'Engine' { 'CardWirthPy_C' }
    'Editor' { 'XEditor_C' }
    'Explorer' { 'DataExplorer_C' }
    default { 'CardWirthPy_C_All' }
}
$executableName = if ($Configuration -eq 'Debug') {
    $executableBaseName + '_debug.exe'
} else {
    $executableBaseName + '.exe'
}
$executablePath = Join-Path $TestEnvironmentDirectory $executableName
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Application executable was not found: $executablePath. Run without -NoBuild first."
}

$startArguments = @{
    FilePath = $executablePath
    WorkingDirectory = $TestEnvironmentDirectory
    PassThru = $true
}
if ($ApplicationArguments.Count -gt 0) {
    $startArguments['ArgumentList'] = $ApplicationArguments
}
if ($Wait) {
    $startArguments['Wait'] = $true
}

Write-Host "Starting $executablePath"
$process = Start-Process @startArguments
if (-not $Wait) {
    Write-Host "Process ID: $($process.Id)"
}
