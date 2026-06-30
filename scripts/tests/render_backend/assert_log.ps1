param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [Parameter(Mandatory = $true)]
    [string]$Pattern,

    [bool]$Literal = $true
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "Log file not found: $LogPath"
}

$match = Select-String -LiteralPath $LogPath -Pattern $Pattern -SimpleMatch:$Literal -Quiet
if (-not $match) {
    throw "Pattern not found in log '$LogPath': $Pattern"
}

Write-Host "Found pattern in log '$LogPath': $Pattern"
