# nefuOS ：/
param(
    [string]$Exe = "D:\mycppos1\nefuOS\dist\nefuOS.exe",
    [string]$Out = "D:\mycppos1\nefuOS\dist\shots"
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
function Click([IntPtr]$h, [int]$x, [int]$y) {
  Send $h $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y
  Send $h $WM_LDOWN ([IntPtr]1) $x $y
  Start-Sleep -Milliseconds 40
  Send $h $WM_LUP ([IntPtr]::Zero) $x $y
  Start-Sleep -Milliseconds 60
}
function DblClick([IntPtr]$h, [int]$x, [int]$y) {
  Click $h $x $y; Start-Sleep -Milliseconds 90; Click $h $x $y; Start-Sleep -Milliseconds 250
}
function Key([IntPtr]$h, [int]$vk) {
  [N]::PostMessage($h,$WM_KEYDOWN,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 25
  [N]::PostMessage($h,$WM_KEYUP,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 25
}
function TypeText([IntPtr]$h, [string]$s) {
  foreach ($c in $s.ToCharArray()) {
    if ($c -eq ' ') { Key $h 0x20 }
    elseif ($c -ge 'a' -and $c -le 'z') { Key $h (0x41 + ([int][char]$c - 97)) }
    elseif ($c -ge '0' -and $c -le '9') { Key $h (0x30 + ([int][char]$c - 48)) }
    else { Key $h ([int][char]$c) }
  }
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
  Write-Output "shot: $name.png (${w}x${ht})"
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
Start-Sleep -Seconds 4
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "FAIL: no window"; exit 1 }
Write-Output "hwnd=$h"

Snap $h "01_desktop"
DblClick $h 60 42   # File Manager
Snap $h "02_filemgr"
DblClick $h 166 42  # Terminal
Snap $h "03_terminal"
TypeText $h "help"
Key $h 0x0D
Start-Sleep -Milliseconds 300
Snap $h "04_terminal_help"
TypeText $h "ls"
Key $h 0x0D
Start-Sleep -Milliseconds 250
Snap $h "05_terminal_ls"
DblClick $h 270 42  # Calculator
Snap $h "06_calc"
DblClick $h 60 162  # Text Viewer
Snap $h "07_textview"
DblClick $h 166 162 # System Info
Snap $h "08_sysinfo"

# start menu -> About
Click $h 36 585
Start-Sleep -Milliseconds 300
Snap $h "09_startmenu"
$aboutY = 357 + 5 * 26 + 12
Click $h 36 $aboutY
Start-Sleep -Milliseconds 400
Snap $h "10_about"

Write-Output "ALL DONE"
