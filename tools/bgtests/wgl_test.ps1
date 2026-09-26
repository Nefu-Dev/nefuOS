$ErrorActionPreference = 'Continue'
$exe = Join-Path $env:TEMP "nefu_fix_test\nefuOS.exe"
$dir = "D:\mycppos1\nefuOS\tools\bgtests"
$srv = Start-Process -FilePath "python" -ArgumentList "-m","http.server","8123","--directory",$dir -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
$log = Join-Path $dir "nefu_wgl_log.txt"
if (Test-Path $log) { Remove-Item $log }
$p = Start-Process -FilePath $exe -ArgumentList "--app","17","--url","http://127.0.0.1:8123/wgl_test.html" -PassThru -RedirectStandardOutput $log
Start-Sleep -Seconds 8
"alive=$(-not $p.HasExited)"
"--- GL log lines ---"
Select-String -Path $log -Pattern "GL:|js:|load|failed" | Select-Object -First 10 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(140, $_.Line.Length)) }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W2 {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
$w = $p.MainWindowHandle
$rect = New-Object W2+RECT
[W2]::GetWindowRect($w, [ref]$rect) | Out-Null
$rw = $rect.R - $rect.L
$rh = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap($rw, $rh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[W2]::PrintWindow($w, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$png = Join-Path $dir "wgl_view.png"
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
"shot: ${rw}x${rh}"

# sample via PIL (window-relative coords, y from top)
$samples = @(
  @(30, 110),   # canvas top-left -> clear color (deep blue)
  @(160, 310),  # canvas lower-middle -> red triangle
  @(170, 120),  # canvas upper area -> clear color
  @(300, 120),  # canvas right -> clear color
  @(10, 80)     # above canvas -> page bg (dark gray)
)
$code = "from PIL import Image; im=Image.open(r'$png').convert('RGB');"
foreach ($sp in $samples) {
  $code += "print('p($($sp[0]),$($sp[1]))=', im.getpixel(($($sp[0]),$($sp[1]))));"
}
python -c $code 2>&1 | Select-Object -First 8

Stop-Process -Name "nefuOS" -Force -ErrorAction SilentlyContinue
Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
"done"