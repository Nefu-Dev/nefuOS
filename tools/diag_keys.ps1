# 逐键定位记事本崩溃
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NK {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$WM_MOUSEMOVE=0x200; $WM_LDOWN=0x201; $WM_LUP=0x202; $WM_KEYDOWN=0x100; $WM_KEYUP=0x101
function Send($hh,$m,$w,$x,$y){ $lp=[IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF)); [NK]::PostMessage($hh,$m,$w,$lp)|Out-Null }
function Click($hh,$x,$y){ Send $hh $WM_MOUSEMOVE ([IntPtr]::Zero) $x $y; Send $hh $WM_LDOWN ([IntPtr]1) $x $y; Start-Sleep -Milliseconds 50; Send $hh $WM_LUP ([IntPtr]::Zero) $x $y; Start-Sleep -Milliseconds 100 }
function DblClick($hh,$x,$y){ Click $hh $x $y; Start-Sleep -Milliseconds 100; Click $hh $x $y; Start-Sleep -Milliseconds 350 }
function Key($hh,$vk){ [NK]::PostMessage($hh,$WM_KEYDOWN,[IntPtr]$vk,[IntPtr]::Zero)|Out-Null; Start-Sleep -Milliseconds 30; [NK]::PostMessage($hh,$WM_KEYUP,[IntPtr]$vk,[IntPtr]::Zero)|Out-Null; Start-Sleep -Milliseconds 30 }
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
$p = Start-Process -FilePath "D:\mycppos1\nefuOS\dist\nefuOS.exe" -WorkingDirectory "D:\mycppos1\nefuOS\dist" -PassThru -RedirectStandardOutput "D:\mycppos1\nefuOS\dist\dbg_out.txt"
Start-Sleep -Seconds 4
$p.Refresh(); $h = $p.MainWindowHandle
DblClick $h 166 264; Start-Sleep -Milliseconds 300
foreach($ry in @(118,176,234,292,350)){ Click $h 515 $ry; Start-Sleep -Milliseconds 150 }
Click $h 558 46; Start-Sleep -Milliseconds 250
Click $h 36 585; Start-Sleep -Milliseconds 250
Click $h 100 493; Start-Sleep -Milliseconds 400
foreach($c in @('H','e','l','l','o',' ','n','e','f','u','o','S','!')){
  Key $h ([int][char]$c)
  Start-Sleep -Milliseconds 120
  if(-not (Get-Process nefuOS -ErrorAction SilentlyContinue)){ Write-Output "CRASH at key: $c"; break }
}
if(Get-Process nefuOS -ErrorAction SilentlyContinue){ Write-Output "ALIVE all keys" }
Get-Process nefuOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 200
Write-Output "DONE"
