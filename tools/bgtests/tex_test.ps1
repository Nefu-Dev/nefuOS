$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$log = Join-Path $dir "nefu_tex_log.txt"
if (Test-Path $log) { Remove-Item $log }
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/tex_test.html" -PassThru -RedirectStandardOutput $log
Start-Sleep -Seconds 8
"alive=$(-not $p.HasExited)"
"--- log ---"
Select-String -Path $log -Pattern "GL:|js:|load|failed" | Select-Object -First 8 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(140, $_.Line.Length)) }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W3 {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
$w = $p.MainWindowHandle
$rect = New-Object W3+RECT
[W3]::GetWindowRect($w, [ref]$rect) | Out-Null
$rw = $rect.R - $rect.L
$rh = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap($rw, $rh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[W3]::PrintWindow($w, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$png = Join-Path $dir "tex_view.png"
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
"shot: ${rw}x${rh}"

$samples = @(
  @(30, 110),
  @(160, 310),
  @(170, 120),
  @(300, 120)
)
$code = "from PIL import Image; im=Image.open(r'$png').convert('RGB');"
foreach ($sp in $samples) {
  $code += "print('p($($sp[0]),$($sp[1]))=', im.getpixel(($($sp[0]),$($sp[1]))));"
}
python -c $code 2>&1 | Select-Object -First 8
Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"done"