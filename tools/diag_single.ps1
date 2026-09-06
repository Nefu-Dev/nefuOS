# ： File Manager， stdout
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ND {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing
$WM_MOUSEMOVE=0x200; $WM_LDOWN=0x201; $WM_LUP=0x202
function Send([IntPtr]$h, [uint32]$m, [IntPtr]$w, [int]$x, [int]$y) {
  $lp = [IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF))
  [ND]::PostMessage($h,$m,$w,$lp) | Out-Null
}
function Click([IntPtr]$h, [int]$x, [int]$y) {
  Send $h $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y
  Send $h $WM_LDOWN ([IntPtr]1) $x $y
  Start-Sleep -Milliseconds 40
  Send $h $WM_LUP ([IntPtr]::Zero) $x $y
  Start-Sleep -Milliseconds 60
}
function DblClick([IntPtr]$h, [int]$x, [int]$y) {
  Click $h $x $y; Start-Sleep -Milliseconds 90; Click $h $x $y; Start-Sleep -Milliseconds 400
}
function Snap([IntPtr]$h, [string]$name) {
  $r = New-Object ND+RECT
  [ND]::GetWindowRect($h,[ref]$r) | Out-Null
  $w = $r.R - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w,$ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  [ND]::PrintWindow($h,$dc,2) | Out-Null
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save("D:\mycppos1\nefuOS\dist\shots\$name.png"); $bmp.Dispose()
  Write-Output "shot: $name"
}
New-Item -ItemType Directory -Force -Path "D:\mycppos1\nefuOS\dist\shots" | Out-Null
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$p = Start-Process -FilePath "D:\mycppos1\nefuOS\dist\nefuOS.exe" -WorkingDirectory "D:\mycppos1\nefuOS\dist" -RedirectStandardOutput "D:\mycppos1\nefuOS\dist\diag_out.txt" -PassThru
Start-Sleep -Seconds 4
$p.Refresh(); $h = $p.MainWindowHandle
Snap $h "d1_before"
DblClick $h 60 42
Snap $h "d2_after_fm"
Write-Output "--- stdout ---"
Get-Content "D:\mycppos1\nefuOS\dist\diag_out.txt"
Stop-Process -Id $p.Id -Force
