param(
    [Parameter(Mandatory = $true)][string]$Small,
    [Parameter(Mandatory = $true)][string]$Large,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'
$entries = @(
    @{ Size = 32; Bytes = [System.IO.File]::ReadAllBytes($Small) },
    @{ Size = 128; Bytes = [System.IO.File]::ReadAllBytes($Large) }
)
foreach ($entry in $entries) {
    $bytes = $entry.Bytes
    if ($bytes.Length -lt 24 -or
        [System.BitConverter]::ToString($bytes, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') {
        throw "Application icon source is not a PNG"
    }
    $size = $entry.Size
    if ($bytes[16] -ne 0 -or $bytes[17] -ne 0 -or $bytes[18] -ne 0 -or
        $bytes[19] -ne $size -or $bytes[20] -ne 0 -or $bytes[21] -ne 0 -or
        $bytes[22] -ne 0 -or $bytes[23] -ne $size) {
        throw "Application icon source is not ${size}x${size}"
    }
}

$stream = [System.IO.File]::Create($Output)
try {
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint16]0) # ICO の予約領域。
    $writer.Write([uint16]1) # アイコン。
    $writer.Write([uint16]$entries.Count)
    $offset = 6 + 16 * $entries.Count
    foreach ($entry in $entries) {
        $writer.Write([byte]$entry.Size)
        $writer.Write([byte]$entry.Size)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$entry.Bytes.Length)
        $writer.Write([uint32]$offset)
        $offset += $entry.Bytes.Length
    }
    foreach ($entry in $entries) { $writer.Write([byte[]]$entry.Bytes) }
    $writer.Flush()
} finally {
    $stream.Dispose()
}
