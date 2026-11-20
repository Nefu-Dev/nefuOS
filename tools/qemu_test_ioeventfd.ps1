$ErrorActionPreference = 'Continue'
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$iso = Join-Path $env:TEMP "nefu_dist\nefuOS.iso"
$out = "D:\mycppos1\nefuOS\dist"
$q   = "D:\mycppos1\nefuOS\tools\qemu\qemu-system-x86_64.exe"
$ev  = Join-Path $out "trace_ev_ok.txt"

foreach ($f in @("qemu_vg_noioev.log","qemu_vs_noioev.log","qemu_tr_noioev.log")) {
  $p = Join-Path $out $f
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}

$a = @("-accel","whpx","-m","256M",
  "-drive","file=$iso,media=cdrom,format=raw","-boot","d",
  "-device","virtio-gpu-pci,ioeventfd=off","-vga","none","-display","none",
  "-debugcon","file:$out\qemu_vg_noioev.log",
  "-serial","file:$out\qemu_vs_noioev.log",
  "-trace","events=$ev","-D","$out\qemu_tr_noioev.log")

$proc = Start-Process -FilePath $q -ArgumentList $a -WindowStyle Hidden -PassThru
Write-Output ("PID=" + $proc.Id)
Start-Sleep -Seconds 30
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Output "killed" } else { Write-Output ("exited=" + $proc.ExitCode) }
