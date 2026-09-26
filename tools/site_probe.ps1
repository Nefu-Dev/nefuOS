param([string]$Url = "https://github.com", [string]$Tag = "gh")
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32d {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static void Raise(IntPtr h) { ShowWindow(h, 9); SetForegroundWindow(h); }
}
"@
Add-Type -AssemblyName System.Drawing
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url",$Url -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { "NO WINDOW"; exit 1 }
[W32d]::Raise($hwnd) | Out-Null
Start-Sleep -Seconds 18
$r = New-Object W32d+RECT
[W32d]::GetClientRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W32d]::PrintWindow($hwnd, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$g.Dispose()
$bmp.Save("D:\mycppos1\nefuOS\tools\bgtests\site_$Tag.bmp", [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
"CAPTURED $Url"
Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
