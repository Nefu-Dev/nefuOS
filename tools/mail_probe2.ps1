param(
    [string]$Exe = "C:\Users\huawei\AppData\Local\Temp\nefu_fix_test\nefuOS.exe",
    [string]$Out = "D:\mycppos1\nefuOS\shots"
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class N {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing

$WM_MOUSEMOVE=0x200; $WM_LDOWN=0x201; $WM_LUP=0x202
$WM_KEYDOWN=0x100; $WM_KEYUP=0x101
function Send([IntPtr]$h, [uint32]$m, [IntPtr]$w, [int]$x, [int]$y) {
  $lp = [IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF))
  [N]::PostMessage($h,$m,$w,$lp) | Out-Null
}
function Key([IntPtr]$h, [int]$vk) {
  [N]::PostMessage($h,$WM_KEYDOWN,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 30
  [N]::PostMessage($h,$WM_KEYUP,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 40
}
function TypeText([IntPtr]$h, [string]$s) {
  foreach ($c in $s.ToCharArray()) {
    if ($c -ge 'a' -and $c -le 'z') { Key $h (0x41 + ([int][char]$c - 97)) }
    elseif ($c -ge '0' -and $c -le '9') { Key $h (0x30 + ([int][char]$c - 48)) }
  }
}
function Click([IntPtr]$h, [int]$x, [int]$y, [int]$ms=400) {
  Send $h $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y
  Send $h $WM_LDOWN ([IntPtr]1) $x $y
  Start-Sleep -Milliseconds 80
  Send $h $WM_LUP ([IntPtr]::Zero) $x $y
  Start-Sleep -Milliseconds $ms
}
function Snap([IntPtr]$h, [string]$name) {
  $r = New-Object N+RECT
  [N]::GetWindowRect($h,[ref]$r) | Out-Null
  $w = $r.R - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w,$ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  [N]::PrintWindow($h,$dc,2) | Out-Null
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save("$Out\$name.png"); $bmp.Dispose()
  Write-Output "shot: $name"
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
Start-Sleep -Seconds 4
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "FAIL: no window"; exit 1 }
Write-Output "hwnd=$h"

TypeText $h "user"; Key $h 0x09
TypeText $h "user"; Key $h 0x09
TypeText $h "user"; Key $h 0x09
Key $h 0x0D
Start-Sleep -Milliseconds 800
TypeText $h "user"; Key $h 0x0D
Start-Sleep -Milliseconds 800
Click $h 364 108 200; Click $h 364 108 600
Snap $h "a0"

# try account row at y=330..400 in steps; screenshot every other
foreach ($yy in 330, 345, 360, 375, 390, 405) {
  Click $h 154 $yy
  Snap $h ("ay" + $yy)
}
Write-Output "PROBE2 DONE"
