param([int]$TargetPid, [string]$Out)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public class WCap {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static string FindRect(uint target){
    string found=null;
    EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p);
      if(p==target && IsWindowVisible(h)){
        var sb=new StringBuilder(256); GetWindowText(h,sb,256);
        RECT r; GetWindowRect(h,out r);
        found=sb+"|"+r.L+","+r.T+","+r.R+","+r.B;
        return false;
      } return true; }, IntPtr.Zero);
    return found;
  }
}
"@
[WCap]::SetProcessDPIAware() | Out-Null
$info=[WCap]::FindRect([uint32]$TargetPid)
if($null -eq $info){ "no visible window for pid $TargetPid"; exit 1 }
"rect: $info"
$parts=$info -split '\|'
$r=$parts[1] -split ','
$L=[int]$r[0]; $T=[int]$r[1]; $R=[int]$r[2]; $B=[int]$r[3]
$w=$R-$L; $h=$B-$T
Add-Type -AssemblyName System.Drawing
$bmp=New-Object System.Drawing.Bitmap($w,$h)
$g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($L,$T,0,0,$bmp.Size)
$bmp.Save($Out,[System.Drawing.Imaging.ImageFormat]::Png)
"captured $w x $h -> $Out"
