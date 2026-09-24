Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32 {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT pt);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
    public static void ClickAbs(int x, int y, int sw, int sh) {
        INPUT[] inp = new INPUT[1];
        inp[0].type = 0;
        inp[0].mi.dx = (int)((x * 65535.0f) / sw);
        inp[0].mi.dy = (int)((y * 65535.0f) / sh);
        inp[0].mi.dwFlags = 0x0001 | 0x8000;
        SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
        inp[0].mi.dwFlags = 0x0002 | 0x8000;
        SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
        inp[0].mi.dwFlags = 0x0004 | 0x8000;
        SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Raise(IntPtr h) { ShowWindow(h, 9); SetForegroundWindow(h); }
}
"@
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$sw = $vs.Width; $sh = $vs.Height
"virtual screen: $sw x $sh"
function Snap([string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($sw, $sh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0,0,0,0,$bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://example.com" -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { "NO WINDOW"; exit 1 }
"window handle: $hwnd"
[W32]::Raise($hwnd) | Out-Null
Start-Sleep -Seconds 18
[W32]::Raise($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object W32+RECT
[W32]::GetClientRect($hwnd, [ref]$r) | Out-Null
$pt = New-Object W32+POINT
$pt.X = 0; $pt.Y = 0
[W32]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
"client origin: $($pt.X),$($pt.Y)"
Snap "D:\mycppos1\nefuOS\build\bare\nav_0_before.bmp"
# --- locate the blue link inside the client framebuffer (800x600) ---
$loc = python -c @"
from PIL import Image
im = Image.open(r'D:\mycppos1\nefuOS\build\bare\nav_0_before.bmp').convert('RGB')
ox, oy = $($pt.X), $($pt.Y)
blue = []
for y in range(0, 600):
    for x in range(0, 800):
        r,g,b = im.getpixel((ox+x, oy+y))
        if b > 180 and r < 120 and g < 180 and r > 40:
            blue.append((x,y))
if not blue:
    print('NOBLUE')
    raise SystemExit
# cluster: link = cluster with y in 200..500; tab = cluster with y < 60
import collections
def cluster(pts, d=8):
    groups = []
    for pt in pts:
        placed = False
        for g in groups:
            if abs(g[0][0]-pt[0]) < d and abs(g[0][1]-pt[1]) < d:
                g.append(pt); g[0] = (sum(p[0] for p in g)//len(g), sum(p[1] for p in g)//len(g))
                placed = True; break
        if not placed:
            groups.append([pt, pt])
    return [g[0] for g in groups]
centers = cluster(blue)
tab = [c for c in centers if c[1] < 60]
link = [c for c in centers if 180 <= c[1] <= 520]
link.sort(key=lambda c: c[1])
if link:
    lx, ly = link[len(link)//2]
    print('LINKCENTER %d %d' % (lx, ly))
else:
    print('NOLINK')
if tab:
    tx, ty = tab[0]
    print('TABCENTER %d %d' % (tx, ty))
"@
"locator: $loc"
if ($loc -match "NOBLUE|NOLINK") { "CANNOT LOCATE LINK"; Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue; exit 1 }
$parts = ($loc | Select-String "LINKCENTER (\d+) (\d+)" | ForEach-Object { $_.Matches[0].Groups })
if ($parts) {
    $lx = [int]$parts[0].Value; $ly = [int]$parts[1].Value
    $tabM = $loc | Select-String "TABCENTER (\d+) (\d+)"
    $tx = 4; $ty = 4
    if ($tabM) { $tx = [int]$tabM.Matches[0].Groups[1].Value; $ty = [int]$tabM.Matches[0].Groups[2].Value }
    "link at fb ($lx,$ly), tab at fb ($tx,$ty)"
    $sx = $pt.X + $lx; $sy = $pt.Y + $ly
    "click link screen ($sx,$sy)"
    [W32]::ClickAbs($sx, $sy, $sw, $sh)
    Start-Sleep -Seconds 16
    Snap "D:\mycppos1\nefuOS\build\bare\nav_1_after_link.bmp"
    # back button: chrome origin = (tab.x - 4, tab.y - 4); back center chrome (16, 44)
    $cx0 = $tx - 4; $cy0 = $ty - 4
    $bx = $pt.X + $cx0 + 16; $by = $pt.Y + $cy0 + 44
    "click back screen ($bx,$by)"
    [W32]::ClickAbs($bx, $by, $sw, $sh)
    Start-Sleep -Seconds 12
    Snap "D:\mycppos1\nefuOS\build\bare\nav_2_after_back.bmp"
}
Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
"NAV TEST DONE"
