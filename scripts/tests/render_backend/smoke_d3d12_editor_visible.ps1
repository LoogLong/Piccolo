param(
    [string]$BuildDir = 'build',
    [string]$Configuration = 'Debug',
    [int]$WarmupSeconds = 10,
    [double]$MinNonBlackRatio = 0.01,
    [double]$RegionX = 0.25,
    [double]$RegionY = 0.14,
    [double]$RegionWidth = 0.47,
    [double]$RegionHeight = 0.46,
    [string]$EditorPath
)

$ErrorActionPreference = 'Stop'

if ($WarmupSeconds -lt 1) {
    throw "WarmupSeconds must be at least 1."
}

if ($MinNonBlackRatio -le 0.0 -or $MinNonBlackRatio -gt 1.0) {
    throw "MinNonBlackRatio must be within (0, 1]."
}

foreach ($regionValue in @($RegionX, $RegionY, $RegionWidth, $RegionHeight)) {
    if ($regionValue -lt 0.0 -or $regionValue -gt 1.0) {
        throw "Capture region values must be within [0, 1]."
    }
}
if ($RegionWidth -le 0.0 -or $RegionHeight -le 0.0 -or
    $RegionX + $RegionWidth -gt 1.0 -or $RegionY + $RegionHeight -gt 1.0) {
    throw "Capture region must be non-empty and fit inside the client area."
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $buildDir = [System.IO.Path]::GetFullPath($BuildDir)
} else {
    $buildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}

if (-not $EditorPath) {
    $EditorPath = Join-Path $buildDir "engine\source\editor\$Configuration\PiccoloEditor.exe"
}
$EditorPath = [System.IO.Path]::GetFullPath($EditorPath)

if (-not (Test-Path -LiteralPath $EditorPath -PathType Leaf)) {
    throw "PiccoloEditor executable not found: $EditorPath"
}

$editorConfigPath = Join-Path ([System.IO.Path]::GetDirectoryName($EditorPath)) 'PiccoloEditor.ini'
if (-not (Test-Path -LiteralPath $editorConfigPath -PathType Leaf)) {
    throw "PiccoloEditor config not found: $editorConfigPath"
}

$stdoutLog = Join-Path $buildDir "test_d3d12_editor_visible.stdout.log"
$stderrLog = Join-Path $buildDir "test_d3d12_editor_visible.stderr.log"
$combinedLog = Join-Path $buildDir "test_d3d12_editor_visible.log"
$capturePath = Join-Path $buildDir "test_d3d12_editor_visible.png"

foreach ($path in @($stdoutLog, $stderrLog, $combinedLog, $capturePath)) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class PiccoloVisibleSmokeWin32 {
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public struct POINT {
        public int X;
        public int Y;
    }
}
'@

function Get-NonBlackRatio {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [System.Drawing.Rectangle]$Region
    )

    $nonBlack = 0L
    $right = [Math]::Min($Bitmap.Width, $Region.X + $Region.Width)
    $bottom = [Math]::Min($Bitmap.Height, $Region.Y + $Region.Height)
    for ($y = $Region.Y; $y -lt $bottom; $y += 2) {
        for ($x = $Region.X; $x -lt $right; $x += 2) {
            $pixel = $Bitmap.GetPixel($x, $y)
            if ($pixel.R -gt 8 -or $pixel.G -gt 8 -or $pixel.B -gt 8) {
                $nonBlack++
            }
        }
    }

    $sampledTotal = [int64][Math]::Ceiling($Region.Width / 2.0) * [int64][Math]::Ceiling($Region.Height / 2.0)
    if ($sampledTotal -eq 0) {
        return 0.0
    }
    return [double]$nonBlack / [double]$sampledTotal
}

$originalConfigBytes = [System.IO.File]::ReadAllBytes($editorConfigPath)
$originalConfigText = [System.IO.File]::ReadAllText($editorConfigPath)
if ($originalConfigText -match '(?m)^\s*RenderBackend\s*=') {
    $updatedConfigText = $originalConfigText -replace '(?m)^\s*RenderBackend\s*=.*$', 'RenderBackend=D3D12'
} else {
    $updatedConfigText = $originalConfigText.TrimEnd([char[]]@("`r", "`n")) + [Environment]::NewLine + 'RenderBackend=D3D12' + [Environment]::NewLine
}

$existing = Get-Process PiccoloEditor -ErrorAction SilentlyContinue
if ($existing) {
    $existing | Stop-Process -Force -ErrorAction SilentlyContinue
}

$process = $null
try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($editorConfigPath, $updatedConfigText, $utf8NoBom)

    Write-Host "Starting PiccoloEditor: $EditorPath"
    Write-Host "Working directory: $repoRoot"
    Write-Host "Warmup seconds: $WarmupSeconds"
    Write-Host "Capture region: x=$RegionX y=$RegionY width=$RegionWidth height=$RegionHeight"

    $process = Start-Process -FilePath $EditorPath `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru `
        -WindowStyle Normal

    Start-Sleep -Seconds $WarmupSeconds
    $process.Refresh()
    if ($process.HasExited) {
        throw "PiccoloEditor exited before visible capture with exit code $($process.ExitCode)."
    }

    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        throw "PiccoloEditor did not expose a main window handle."
    }

    [PiccoloVisibleSmokeWin32]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 300

    $rect = New-Object PiccoloVisibleSmokeWin32+RECT
    if (-not [PiccoloVisibleSmokeWin32]::GetClientRect($handle, [ref]$rect)) {
        throw "Failed to query PiccoloEditor client rect."
    }
    $origin = New-Object PiccoloVisibleSmokeWin32+POINT
    $origin.X = 0
    $origin.Y = 0
    if (-not [PiccoloVisibleSmokeWin32]::ClientToScreen($handle, [ref]$origin)) {
        throw "Failed to convert PiccoloEditor client origin."
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 1 -or $height -le 1) {
        throw "PiccoloEditor client area is invalid: ${width}x${height}."
    }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
        $bitmap.Save($capturePath, [System.Drawing.Imaging.ImageFormat]::Png)
        $captureRegion = New-Object System.Drawing.Rectangle(
            [int][Math]::Round($width * $RegionX),
            [int][Math]::Round($height * $RegionY),
            [int][Math]::Round($width * $RegionWidth),
            [int][Math]::Round($height * $RegionHeight))
        $ratio = Get-NonBlackRatio -Bitmap $bitmap -Region $captureRegion
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    Copy-Item -LiteralPath $stdoutLog -Destination $combinedLog -Force
    if (Test-Path -LiteralPath $stderrLog -PathType Leaf) {
        Get-Content -LiteralPath $stderrLog | Add-Content -LiteralPath $combinedLog
    }

    if (-not (Select-String -LiteralPath $combinedLog -Pattern 'Initialized RHI backend: D3D12' -SimpleMatch -Quiet)) {
        throw "D3D12 backend initialization was not observed in $combinedLog."
    }
    if (-not (Select-String -LiteralPath $combinedLog -Pattern 'engine start' -SimpleMatch -Quiet)) {
        throw "Engine start was not observed in $combinedLog."
    }

    Write-Host "Captured PiccoloEditor client: $capturePath (${width}x${height})"
    Write-Host ("Non-black sampled pixel ratio in capture region: {0:P4}" -f $ratio)

    if ($ratio -lt $MinNonBlackRatio) {
        throw "PiccoloEditor D3D12 capture region appears blank. Non-black sampled pixel ratio $ratio is below $MinNonBlackRatio. Capture: $capturePath"
    }
} finally {
    [System.IO.File]::WriteAllBytes($editorConfigPath, $originalConfigBytes)

    if ($process -ne $null) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            Start-Sleep -Seconds 2
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
