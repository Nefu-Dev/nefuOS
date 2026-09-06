# nefuOS ： -> settings -> start menu -> /paint/notepad
param(
    [string]$Exe = "D:\mycppos1\nefuOS\dist\nefuOS.exe",
    [string]$Out = "D:\mycppos1\nefuOS\dist\shots"
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NF {
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
  [NF]::PostMessage($h,$m,$w,$lp) | Out-Null
}
function Click([IntPtr]$h, [int]$x, [int]$y) {
  Send $h $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y
  Send $h $WM_LDOWN ([IntPtr]1) $x $y
  Start-Sleep -Milliseconds 40
  Send $h $WM_LUP ([IntPtr]::Zero) $x $y
  Start-Sleep -Milliseconds 80
}
function DblClick([IntPtr]$h, [int]$x, [int]$y) {
  Click $h $x $y; Start-Sleep -Milliseconds 100; Click $h $x $y; Start-Sleep -Milliseconds 350
}
function Key([IntPtr]$h, [int]$vk) {
  [NF]::PostMessage($h,$WM_KEYDOWN,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 25
  [NF]::PostMessage($h,$WM_KEYUP,[IntPtr]$vk,[IntPtr]::Zero) | Out-Null
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
  for ($try = 0; $try -lt 3; $try++) {
    try {
      $r = New-Object NF+RECT
      if (-not [NF]::GetWindowRect($h,[ref]$r)) { Start-Sleep -Milliseconds 120; continue }
      $w = $r.R - $r.L; $ht = $r.B - $r.T
      if ($w -le 0 -or $ht -le 0) { Start-Sleep -Milliseconds 120; continue }
      $bmp = New-Object System.Drawing.Bitmap($w,$ht)
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $dc = $g.GetHdc()
      [NF]::PrintWindow($h,$dc,2) | Out-Null
      $g.ReleaseHdc($dc); $g.Dispose()
      $bmp.Save("$Out\$name.png"); $bmp.Dispose()
      Write-Output "shot: $name.png"
      return
    } catch { Start-Sleep -Milliseconds 120 }
  }
  Write-Output "WARN: shot failed $name"
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

# 1
Snap $h "n1_desktop"

# 2 （no.32column icons,center 166,264）
DblClick $h 166 264
Start-Sleep -Milliseconds 400
Snap $h "n2_store"

# 3 install 5 （line i: y=118+i*58, x=515）
foreach ($rowY in @(118,176,234,292,350)) {
  Click $h 515 $rowY
  Start-Sleep -Milliseconds 200
}
Snap $h "n3_store_all_installed"
# （558,46），
Click $h 558 46
Start-Sleep -Milliseconds 300

# 4 （no.31column icons,center 62,264）
DblClick $h 62 264
Start-Sleep -Milliseconds 400
Snap $h "n4_settings"

# 5 primary color Green（ x=78,y=64; 224,155）
Click $h 224 155
Start-Sleep -Milliseconds 300
Snap $h "n5_accent_green"

# 6 wallpaper Sunset（224,219）
Click $h 224 219
Start-Sleep -Milliseconds 300
Snap $h "n6_wallpaper_sunset"

# 7 （170,259）： ->
Click $h 170 259
Start-Sleep -Milliseconds 250
Snap $h "n7_clock_off"
Click $h 170 259
Start-Sleep -Milliseconds 250
Snap $h "n8_clock_on"

# 8 (486,73)
Click $h 486 73
Start-Sleep -Milliseconds 300
Snap $h "n8b_desktop_clean"

# 9 start menu：13 app + Power Off; mh=380, my=190
# line: 0..5 built-in(FM..About), 6 Settings, 7 Store, 8 Snake, 9 Paint, 10 Clock, 11 Notepad, 12 Minesweeper, 13 PowerOff
# = 207 + row*26
Click $h 36 585
Start-Sleep -Milliseconds 350
Snap $h "n9_startmenu"

# 10 Snake（row8: 207+208=415）
Click $h 100 415
Start-Sleep -Milliseconds 600
Snap $h "n10_snake"
# Snake（window 106,88 -> 514,97）
Click $h 514 97
Start-Sleep -Milliseconds 250

# 11 Minesweeper（row12: 207+312=519）
Click $h 36 585
Start-Sleep -Milliseconds 300
Click $h 100 519
Start-Sleep -Milliseconds 500
Snap $h "n11_minesweeper"
# （window 134,112 -> 452,121）
Click $h 452 121
Start-Sleep -Milliseconds 250

# 12 Paint（row9: 207+234=441）
Click $h 36 585
Start-Sleep -Milliseconds 300
Click $h 100 441
Start-Sleep -Milliseconds 500
Snap $h "n13_paint"
# （paint window 162,136; y=188）
Click $h 220 220; Click $h 280 270; Click $h 340 220; Click $h 400 270
Start-Sleep -Milliseconds 200
Snap $h "n14_paint_drawn"

# 13 Notepad（row11: 207+286=493）
Click $h 36 585
Start-Sleep -Milliseconds 300
Click $h 100 493
Start-Sleep -Milliseconds 400
Snap $h "n15_notepad"
TypeText $h "Hello nefuOS!"; Key $h 0x0D; TypeText $h "This is my tiny OS"
Key $h 0x1B  # Esc save
Start-Sleep -Milliseconds 300
Snap $h "n16_notepad_saved"

Write-Output "ALL DONE"
