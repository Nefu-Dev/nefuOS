$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32b {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT pt);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    public static void Raise(IntPtr h) { ShowWindow(h, 9); SetForegroundWindow(h); }
}
"@
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$sw = $vs.Width; $sh = $vs.Height
function Snap([string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($sw, $sh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0,0,0,0,$bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { "NO WINDOW"; exit 1 }
[W32b]::Raise($hwnd) | Out-Null
Start-Sleep -Seconds 15
[W32b]::Raise($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object W32b+RECT
[W32b]::GetClientRect($hwnd, [ref]$r) | Out-Null
$pt = New-Object W32b+POINT
$pt.X = 0; $pt.Y = 0
[W32b]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
Snap "D:\mycppos1\nefuOS\tools\bgtests\shot_0.bmp"
"client origin: $($pt.X),$($pt.Y) size $($r.R - $r.L) x $($r.B - $r.T)"
# sample the page area (below chrome ~58px, inside the 800x600 client)
$ox = $pt.X; $oy = $pt.Y
python -c @"
from PIL import Image
im = Image.open(r'D:\mycppos1\nefuOS\tools\bgtests\shot_0.bmp').convert('RGB')
ox, oy = $($pt.X), $($pt.Y)
# page region: client 800x600, chrome top ~58px
samples = {}
for (x, y) in [(100, 120), (300, 150), (600, 250), (150, 400), (500, 480)]:
    r, g, b = im.getpixel((ox+x, oy+y))
    samples['(%d,%d)' % (x, y)] = (r, g, b)
print(samples)
red = sum(1 for v in samples.values() if v[0] > 150 and v[1] < 100 and v[2] < 100)
print('RED_PIXELS', red)
"@
Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
"BG TEST DONE"
