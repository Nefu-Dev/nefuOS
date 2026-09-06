# ：
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NI {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing
$WM_MOUSEMOVE=0x200; $WM_LDOWN=0x201; $WM_LUP=0x202
$WM_KEYDOWN=0x100; $WM_KEYUP=0x101
function Send([IntPtr]$hh,[uint32]$m,[IntPtr]$w,[int]$x,[int]$y){ $lp=[IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF)); [NI]::PostMessage($hh,$m,$w,$lp)|Out-Null }
function Click([IntPtr]$hh,[int]$x,[int]$y){ Send $hh $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y; Send $hh $WM_LDOWN ([IntPtr]1) $x $y; Start-Sleep -Milliseconds 50; Send $hh $WM_LUP ([IntPtr]::Zero) $x $y; Start-Sleep -Milliseconds 100 }
function DblClick([IntPtr]$hh,[int]$x,[int]$y){ Click $hh $x $y; Start-Sleep -Milliseconds 100; Click $hh $x $y; Start-Sleep -Milliseconds 350 }
function Key([IntPtr]$hh,[int]$vk){ [NI]::PostMessage($hh,$WM_KEYDOWN,[IntPtr]$vk,[IntPtr]::Zero)|Out-Null; Start-Sleep -Milliseconds 30; [NI]::PostMessage($hh,$WM_KEYUP,[IntPtr]$vk,[IntPtr]::Zero)|Out-Null; Start-Sleep -Milliseconds 30 }
function TypeText([IntPtr]$hh,[string]$s){ foreach($c in $s.ToCharArray()){ if($c -eq ' '){Key $hh 0x20} elseif($c -ge 'a' -and $c -le 'z'){Key $hh (0x41+([int][char]$c-97))} elseif($c -ge '0' -and $c -le '9'){Key $hh (0x30+([int][char]$c-48))} else {Key $hh ([int][char]$c)} } }
function Snap([IntPtr]$hh,[string]$name){
  for($try=0;$try -lt 3;$try++){ try {
    $r=New-Object NI+RECT
    if(-not [NI]::GetWindowRect($hh,[ref]$r)){ Start-Sleep -Milliseconds 120; continue }
    $bmp=New-Object System.Drawing.Bitmap(($r.R-$r.L),($r.B-$r.T))
    $g=[System.Drawing.Graphics]::FromImage($bmp); $dc=$g.GetHdc()
    [NI]::PrintWindow($hh,$dc,2)|Out-Null
    $g.ReleaseHdc($dc); $g.Dispose()
    $bmp.Save("D:\mycppos1\nefuOS\dist\shots\$name.png"); $bmp.Dispose()
    Write-Output "shot: $name"; return
  } catch { Start-Sleep -Milliseconds 120 } }
  Write-Output "shot failed: $name"
}
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
$p = Start-Process -FilePath "D:\mycppos1\nefuOS\dist\nefuOS.exe" -WorkingDirectory "D:\mycppos1\nefuOS\dist" -PassThru
Start-Sleep -Seconds 4
$p.Refresh(); $h = $p.MainWindowHandle
Write-Output "hwnd=$h"
# （ 5 ，Notepad row11）
DblClick $h 166 264; Start-Sleep -Milliseconds 300
foreach($ry in @(118,176,234,292,350)){ Click $h 515 $ry; Start-Sleep -Milliseconds 200 }
Click $h 558 46; Start-Sleep -Milliseconds 300    # close store
Click $h 36 585; Start-Sleep -Milliseconds 300    # start menu
Click $h 100 493; Start-Sleep -Milliseconds 500   # Notepad (row11, my=190: 207+286=493)
Snap $h "p1_notepad_open"
# ，
foreach($chunk in @("Hello ", "nefuOS!", "This is my tiny OS")){
  TypeText $h $chunk
  Start-Sleep -Milliseconds 200
  if(-not (Get-Process nefuOS -ErrorAction SilentlyContinue)){ Write-Output "CRASH after typing: $chunk"; break }
  Snap $h ("p2_typed_" + ($chunk -replace '[^a-zA-Z0-9]','_'))
}
if(Get-Process nefuOS -ErrorAction SilentlyContinue){
  Key $h 0x0D   # Enter newline
  Start-Sleep -Milliseconds 300
  if(-not (Get-Process nefuOS -ErrorAction SilentlyContinue)){ Write-Output "CRASH after Enter" }
  Snap $h "p3_after_enter"
  Key $h 0x1B   # Esc save
  Start-Sleep -Milliseconds 400
  if(Get-Process nefuOS -ErrorAction SilentlyContinue){ Write-Output "ALIVE after ESC save"; Snap $h "p4_saved" }
  else { Write-Output "CRASH after ESC save" }
}
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Output "DONE"
