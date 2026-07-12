# PrintWindow capture of a window by title substring. Tries PW_RENDERFULLCONTENT (0x2)
# first (works for D3D12 swapchains), falls back to plain PrintWindow.
param(
  [Parameter(Mandatory=$true)][string]$TitleSubstring,
  [Parameter(Mandatory=$true)][string]$OutFile
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hDC, uint flags);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@ -ReferencedAssemblies System.Drawing

$found = [IntPtr]::Zero
$callback = [Win32+EnumWindowsProc]{
    param($hWnd, $lParam)
    if ([Win32]::IsWindowVisible($hWnd)) {
        $sb = New-Object System.Text.StringBuilder 256
        [Win32]::GetWindowText($hWnd, $sb, 256) | Out-Null
        $t = $sb.ToString()
        if ($t -and $t.Contains($TitleSubstring)) {
            $script:found = $hWnd
            return $false
        }
    }
    return $true
}
[Win32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null

if ($found -eq [IntPtr]::Zero) {
    Write-Host "WINDOW_NOT_FOUND: $TitleSubstring"
    exit 1
}
[Win32]::ShowWindow($found, 9) | Out-Null  # SW_RESTORE
Start-Sleep -Milliseconds 200
[Win32]::SetForegroundWindow($found) | Out-Null
Start-Sleep -Milliseconds 500

$r = New-Object Win32+RECT
[Win32]::GetWindowRect($found, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
Write-Host "WINDOW_FOUND hwnd=$found rect=${w}x${h} title=$([char[]]($sb.ToString() -replace '\s+',' ') | Select -First 40)"
if ($w -le 0 -or $h -le 0) { Write-Host "BAD_RECT"; exit 2 }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# 0x2 = PW_RENDERFULLCONTENT (Win 8.1+, captures D3D12 swapchain content)
$ok = [Win32]::PrintWindow($found, $hdc, 0x2)
if (-not $ok) {
    Write-Host "PrintWindow(0x2) failed, retrying with flag 0"
    $ok = [Win32]::PrintWindow($found, $hdc, 0)
}
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved: $OutFile (${w}x${h}, ok=$ok)"
