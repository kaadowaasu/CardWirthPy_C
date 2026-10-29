#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CliArgs = @()
)

$ErrorActionPreference = "Stop"

$usage = @'
Usage:
  .\start_local_server.ps1 --mode https [--port PORT] [--root PATH] [--no-browser]
  .\start_local_server.ps1 --mode http  [--port PORT] [--root PATH] [--no-browser]
  .\start_local_server.ps1 --help

Required:
  --mode https   HTTPS on the active private LAN IPv4 address. Recommended for phones.
  --mode http    Plain HTTP on the active private LAN IPv4 address. Use only for diagnostics;
                 mobile browsers may block secure-origin features such as threaded WebAssembly.

Options:
  --port PORT    Listening port (default: 8001).
  --root PATH    Web root (default: this release directory).
  --no-browser  Do not open the URL on this PC.
  --help         Show this help. This option does not require --mode.

For phone access, connect the phone and PC to the same private Wi-Fi and open the LAN URL
printed by the script (do not use 127.0.0.1). HTTPS also requires trusting the listed
certificate on the phone. If the request times out, allow this TCP port on the Private
network profile only. Keep this PowerShell window open while the server is running.
'@

$CliArgs = @($CliArgs)
$mode = $null
$port = 8001
$rootOption = $null
$noBrowser = $false

for ($i = 0; $i -lt $CliArgs.Count; $i++) {
    $option = [string]$CliArgs[$i]

    if ($option -in @("--help", "-h")) {
        Write-Output $usage
        return
    }

    if ($option -in @("--mode", "-mode")) {
        if ($null -ne $mode) { throw "--mode may be specified only once." + [Environment]::NewLine + $usage }
        if (($i + 1) -ge $CliArgs.Count -or ([string]$CliArgs[$i + 1]).StartsWith("--")) {
            throw "Missing value for --mode." + [Environment]::NewLine + $usage
        }
        $mode = ([string]$CliArgs[++$i]).ToLowerInvariant()
        if ($mode -notin @("https", "http")) {
            throw "Invalid --mode '$mode'. Use https or http." + [Environment]::NewLine + $usage
        }
        continue
    }

    if ($option -in @("--port", "-port")) {
        if (($i + 1) -ge $CliArgs.Count -or ([string]$CliArgs[$i + 1]).StartsWith("--")) {
            throw "Missing value for --port." + [Environment]::NewLine + $usage
        }
        $parsedPort = 0
        if (-not [int]::TryParse([string]$CliArgs[++$i], [ref]$parsedPort) -or
            $parsedPort -lt 1 -or $parsedPort -gt 65535) {
            throw "--port must be an integer from 1 to 65535." + [Environment]::NewLine + $usage
        }
        $port = $parsedPort
        continue
    }

    if ($option -in @("--root", "-root")) {
        if (($i + 1) -ge $CliArgs.Count -or ([string]$CliArgs[$i + 1]).StartsWith("--")) {
            throw "Missing value for --root." + [Environment]::NewLine + $usage
        }
        $rootOption = [string]$CliArgs[++$i]
        continue
    }

    if ($option -in @("--no-browser", "-nobrowser")) {
        $noBrowser = $true
        continue
    }

    throw "Unknown option '$option'." + [Environment]::NewLine + $usage
}

if ($null -eq $mode) {
    throw "Required option missing: --mode https|http." + [Environment]::NewLine + $usage
}

if ($rootOption) {
    if (-not (Test-Path -LiteralPath $rootOption -PathType Container)) {
        throw "Web root not found: $rootOption"
    }
    $root = (Resolve-Path -LiteralPath $rootOption).Path
} else {
    $root = $PSScriptRoot
    if ((Split-Path -Leaf $root) -ieq "py_c") {
        $root = Split-Path -Parent $root
    }
    if (-not (Test-Path -LiteralPath (Join-Path $root "serve.py") -PathType Leaf)) {
        $defaultRelease = "D:\codes\webcardwirth\release\webcardwirth_release"
        if (Test-Path -LiteralPath (Join-Path $defaultRelease "serve.py") -PathType Leaf) {
            $root = $defaultRelease
        }
    }
    $root = (Resolve-Path -LiteralPath $root).Path
}

$serveScript = Join-Path $root "serve.py"
if (-not (Test-Path -LiteralPath $serveScript -PathType Leaf)) {
    throw "serve.py was not found under: $root"
}
if (-not (Test-Path -LiteralPath (Join-Path $root "index.html") -PathType Leaf)) {
    throw "index.html was not found under: $root"
}

function Get-PrimaryPrivateIPv4 {
    $addresses = @()
    try {
        $addresses = @(
            Get-NetIPConfiguration -ErrorAction Stop |
                Where-Object { $_.NetAdapter.Status -eq "Up" -and $_.IPv4DefaultGateway } |
                ForEach-Object {
                    $_.IPv4Address | ForEach-Object { [string]$_.IPAddress }
                }
        )
    } catch {
        # 制限された Windows 環境では、下の route.exe を代替として使う。
    }

    if ($addresses.Count -eq 0) {
        try {
            $routes = @(route.exe print -4 2>$null |
                Select-String -Pattern "^\s*0\.0\.0\.0\s+0\.0\.0\.0")
            foreach ($route in $routes) {
                $found = [regex]::Matches(
                    [string]$route.Line,
                    "\b(?:\d{1,3}\.){3}\d{1,3}\b"
                )
                if ($found.Count -ge 4) {
                    $addresses += $found[3].Value
                }
            }
        } catch {
            # 下で対応に必要なエラーを報告する。
        }
    }

    $privateAddresses = @(
        $addresses |
            Where-Object {
                $_ -match "^10\." -or
                $_ -match "^192\.168\." -or
                $_ -match "^172\.(1[6-9]|2[0-9]|3[01])\."
            } |
            Sort-Object -Unique
    )
    if ($privateAddresses.Count -eq 0) {
        throw "No private LAN IPv4 address was found. Connect this PC to a private Wi-Fi or LAN."
    }

    $ranked = foreach ($address in $privateAddresses) {
        $rank = if ($address -like "192.168.*") { 0 }
                elseif ($address -like "10.*") { 1 }
                else { 2 }
        [pscustomobject]@{ Rank = $rank; Address = $address }
    }
    return ($ranked | Sort-Object Rank, Address | Select-Object -First 1).Address
}

$lanAddress = Get-PrimaryPrivateIPv4
$scheme = $mode
$certificateFile = $null
$keyFile = $null

$pythonCommand = Get-Command python.exe -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
$pythonPrefix = @()
if (-not $pythonCommand) {
    $pythonCommand = Get-Command py.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($pythonCommand) { $pythonPrefix += "-3" }
}
if (-not $pythonCommand) {
    throw "Python 3 was not found. Install Python 3 and retry."
}

if ($mode -eq "https") {
    $certificateRoot = Join-Path (Split-Path -Parent $root) "local_https"
    $certificateFile = Join-Path $certificateRoot "cardwirth-lan.crt"
    $keyFile = Join-Path $certificateRoot "cardwirth-lan.key"
    if (-not (Test-Path -LiteralPath $certificateFile -PathType Leaf)) {
        throw "HTTPS certificate not found: $certificateFile"
    }
    if (-not (Test-Path -LiteralPath $keyFile -PathType Leaf)) {
        throw "HTTPS private key not found: $keyFile"
    }

    $certificateCheck = @'
import ssl, sys, time
path, expected_ip = sys.argv[1:3]
certificate = ssl._ssl._test_decode_cert(path)
sans = certificate.get("subjectAltName", ())
if ("IP Address", expected_ip) not in sans:
    raise SystemExit(f"Certificate SAN does not contain this LAN IP: {expected_ip}")
if ssl.cert_time_to_seconds(certificate["notBefore"]) > time.time():
    raise SystemExit("The HTTPS certificate is not valid yet.")
if ssl.cert_time_to_seconds(certificate["notAfter"]) <= time.time():
    raise SystemExit("The HTTPS certificate has expired.")
print("Certificate SAN and validity: OK")
'@
    # Windows PowerShell 5.1 は複数行のネイティブ -c 引数を
    # 分割し得る。検証処理を符号化し、Python に安定した一つの引数として渡す。
    $certificateCheckBase64 = [Convert]::ToBase64String(
        [System.Text.Encoding]::UTF8.GetBytes($certificateCheck)
    )
    $certificateRunner = "import base64; exec(base64.b64decode('$certificateCheckBase64').decode())"
    $certificateArguments = @($pythonPrefix) + @("-c", $certificateRunner, $certificateFile, $lanAddress)
    & $pythonCommand.Source @certificateArguments
    $certificateExitCode = $LASTEXITCODE
    if ($certificateExitCode -ne 0) {
        throw "HTTPS certificate preflight failed with Python exit code $certificateExitCode. See the diagnostic above. Certificate: $certificateFile; expected LAN IP: $lanAddress."
    }

    $certificateHash = (Get-FileHash -LiteralPath $certificateFile -Algorithm SHA256).Hash
}

$bindAddress = $lanAddress
$serverUrl = "{0}://{1}:{2}/" -f $scheme, $lanAddress, $port
$serverArguments = @(
    $serveScript,
    "--root", $root,
    "--host", $bindAddress,
    "--port", [string]$port
)
if ($mode -eq "https") {
    $serverArguments += @("--certfile", $certificateFile, "--keyfile", $keyFile)
}
if (-not $noBrowser) {
    $serverArguments += "--open"
}
$pythonArguments = @($pythonPrefix) + @($serverArguments)

Write-Host "CardWirth local web server"
Write-Host "Mode: $mode"
Write-Host "Web root: $root"
Write-Host "LAN URL: $serverUrl"
if ($mode -eq "https") {
    Write-Host "Public certificate: $certificateFile"
    Write-Host "Certificate SHA-256: $certificateHash"
    Write-Host "Phone access requires this certificate to be installed and trusted on the phone."
} else {
    Write-Warning "HTTP is unencrypted; mobile secure-origin features, including threaded WebAssembly, may be unavailable."
}
Write-Host "Connect the phone and PC to the same private Wi-Fi. Guest Wi-Fi may block device-to-device access."
Write-Host "If the phone times out, allow TCP port $port on the Private network profile only."
Write-Host "Keep this PowerShell window open. Press Ctrl+C to stop the server."

$exitCode = 0
try {
    & $pythonCommand.Source @pythonArguments
    if ($null -ne $LASTEXITCODE) {
        $exitCode = $LASTEXITCODE
    }
} finally {
    Write-Host "CardWirth local web server stopped."
}

if ($exitCode -notin @(0, -1, -1073741510)) {
    throw "CardWirth web server exited with code $exitCode."
}
