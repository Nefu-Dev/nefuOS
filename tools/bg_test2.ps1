$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32c {
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
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { "NO WINDOW"; exit 1 }
[W32c]::Raise($hwnd) | Out-Null
Start-Sleep -Seconds 15
$r = New-Object W32c+RECT
[W32c]::GetClientRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
"client $w x $h"
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W32c]::PrintWindow($hwnd, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$g.Dispose()
$bmp.Save("D:\mycppos1\nefuOS\tools\bgtests\pw_0.bmp", [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
"PW CAPTURED"
python -c @"
from PIL import Image
im = Image.open(r'D:\mycppos1\nefuOS\tools\bgtests\pw_0.bmp').convert('RGB')
samples = {}
for (x, y) in [(100, 120), (300, 150), (600, 250), (150, 400), (500, 480)]:
    r, g, b = im.getpixel((x, y))
    samples['(%d,%d)' % (x, y)] = (r, g, b)
print(samples)
red = sum(1 for v in samples.values() if v[0] > 150 and v[1] < 100 and v[2] < 100)
print('RED_PIXELS', red)
"@
Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
"PW BG TEST DONE"
