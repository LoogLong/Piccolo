param(
    [string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 20,
    [string]$EditorPath
)

$ErrorActionPreference = 'Stop'

if ($TimeoutSeconds -lt 1) {
    throw "TimeoutSeconds must be at least 1."
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$buildDir = Join-Path $repoRoot 'build'

if (-not $EditorPath) {
    $engineDir = Join-Path $buildDir 'engine'
    $sourceDir = Join-Path $engineDir 'source'
    $editorDir = Join-Path $sourceDir 'editor'
    $configDir = Join-Path $editorDir $Configuration
    $EditorPath = Join-Path $configDir 'PiccoloEditor.exe'
}

$EditorPath = [System.IO.Path]::GetFullPath($EditorPath)

if (-not (Test-Path -LiteralPath $EditorPath -PathType Leaf)) {
    throw "PiccoloEditor executable not found: $EditorPath"
}

if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$stdoutLog = Join-Path $buildDir 'test_d3d12_boot.stdout.log'
$stderrLog = Join-Path $buildDir 'test_d3d12_boot.stderr.log'
$combinedLog = Join-Path $buildDir 'test_d3d12_boot.log'

foreach ($logPath in @($stdoutLog, $stderrLog, $combinedLog)) {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
}

Write-Host "Starting PiccoloEditor: $EditorPath"
Write-Host "Working directory: $repoRoot"
Write-Host "Timeout seconds: $TimeoutSeconds"

$process = Start-Process -FilePath $EditorPath `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -PassThru `
    -WindowStyle Hidden

$nonZeroExitCode = $null
$exited = $process.WaitForExit($TimeoutSeconds * 1000)
if (-not $exited) {
    Write-Host "PiccoloEditor is still running after $TimeoutSeconds seconds; stopping it for smoke test cleanup."
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    $process.WaitForExit()
} elseif ($process.ExitCode -ne 0) {
    $nonZeroExitCode = $process.ExitCode
} else {
    Write-Host "PiccoloEditor exited before timeout with exit code 0."
}

if (-not (Test-Path -LiteralPath $stdoutLog -PathType Leaf)) {
    New-Item -ItemType File -Path $stdoutLog | Out-Null
}

if (-not (Test-Path -LiteralPath $stderrLog -PathType Leaf)) {
    New-Item -ItemType File -Path $stderrLog | Out-Null
}

Copy-Item -LiteralPath $stdoutLog -Destination $combinedLog -Force
Get-Content -LiteralPath $stderrLog | Add-Content -LiteralPath $combinedLog

if ($null -ne $nonZeroExitCode) {
    throw "PiccoloEditor exited before timeout with nonzero exit code: $nonZeroExitCode. Combined log: $combinedLog"
}

$assertLog = Join-Path $PSScriptRoot 'assert_log.ps1'
& $assertLog -LogPath $combinedLog -Pattern 'Initialized RHI backend: D3D12'
& $assertLog -LogPath $combinedLog -Pattern 'engine start'

$fallbackFound = Select-String -LiteralPath $combinedLog -Pattern 'Falling back to Vulkan backend' -SimpleMatch -Quiet
if ($fallbackFound) {
    throw "Forbidden pattern found in log '$combinedLog': Falling back to Vulkan backend"
}

Write-Host "D3D12 backend boot smoke log checks passed: $combinedLog"
