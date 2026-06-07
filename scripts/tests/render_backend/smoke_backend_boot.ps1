param(
    [string]$Configuration = 'Debug',
    [int]$TimeoutSeconds = 20,
    [string]$EditorPath,
    [ValidateSet('Auto', 'Vulkan', 'D3D12')]
    [string]$RenderBackend = 'D3D12',
    [ValidateSet('Vulkan', 'D3D12')]
    [string]$ExpectedBackend,
    [switch]$DisallowFallback
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

$editorConfigPath = Join-Path ([System.IO.Path]::GetDirectoryName($EditorPath)) 'PiccoloEditor.ini'
if (-not (Test-Path -LiteralPath $editorConfigPath -PathType Leaf)) {
    throw "PiccoloEditor config not found: $editorConfigPath"
}

if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

if (-not $ExpectedBackend) {
    if ($RenderBackend -eq 'Vulkan') {
        $ExpectedBackend = 'Vulkan'
    } else {
        $ExpectedBackend = 'D3D12'
    }
}

$logBackendName = if ($RenderBackend -eq $ExpectedBackend) {
    $ExpectedBackend.ToLowerInvariant()
} else {
    ($RenderBackend.ToLowerInvariant() + '_' + $ExpectedBackend.ToLowerInvariant())
}
$stdoutLog = Join-Path $buildDir "test_${logBackendName}_boot.stdout.log"
$stderrLog = Join-Path $buildDir "test_${logBackendName}_boot.stderr.log"
$combinedLog = Join-Path $buildDir "test_${logBackendName}_boot.log"

foreach ($logPath in @($stdoutLog, $stderrLog, $combinedLog)) {
    Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
}

Write-Host "Starting PiccoloEditor: $EditorPath"
Write-Host "Working directory: $repoRoot"
Write-Host "Timeout seconds: $TimeoutSeconds"
Write-Host "Render backend override: $RenderBackend"
Write-Host "Expected initialized backend: $ExpectedBackend"

$originalConfigBytes = [System.IO.File]::ReadAllBytes($editorConfigPath)
$originalConfigText = [System.IO.File]::ReadAllText($editorConfigPath)
if ($originalConfigText -match '(?m)^\s*RenderBackend\s*=') {
    $updatedConfigText = $originalConfigText -replace '(?m)^\s*RenderBackend\s*=.*$', "RenderBackend=$RenderBackend"
} else {
    $updatedConfigText = $originalConfigText.TrimEnd([char[]]@("`r", "`n")) + [Environment]::NewLine + "RenderBackend=$RenderBackend" + [Environment]::NewLine
}

try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($editorConfigPath, $updatedConfigText, $utf8NoBom)

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
} finally {
    [System.IO.File]::WriteAllBytes($editorConfigPath, $originalConfigBytes)
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

$fallbackFound = Select-String -LiteralPath $combinedLog -Pattern 'Falling back to Vulkan backend' -SimpleMatch -Quiet
if ($DisallowFallback -and $fallbackFound) {
    throw "D3D12-primary validation forbids fallback to Vulkan. Combined log: $combinedLog"
}

$assertLog = Join-Path $PSScriptRoot 'assert_log.ps1'
& $assertLog -LogPath $combinedLog -Pattern "Initialized RHI backend: $ExpectedBackend"
& $assertLog -LogPath $combinedLog -Pattern 'engine start'

Write-Host "$ExpectedBackend backend boot smoke log checks passed: $combinedLog"
