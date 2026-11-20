$ErrorActionPreference = 'Continue'
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$iso = Join-Path $env:TEMP "nefu_dist\nefuOS.iso"
$out = "D:\mycppos1\nefuOS\dist"
$q   = "D:\mycppos1\nefuOS\tools\qemu\qemu-system-x86_64.exe"
$ev  = Join-Path $out "trace_ev_ok.txt"

foreach ($f in @("qemu_vg_tcgs.log","qemu_vs_tcgs.log","qemu_tr_tcgs.log")) {
  $p = Join-Path $out $f
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}

$a = @("-accel","tcg,thread=multi","-m","256M",
  "-drive","file=$iso,media=cdrom,format=raw","-boot","d",
  "-device","virtio-gpu-pci","-vga","none","-display","none",
  "-debugcon","file:$out\qemu_vg_tcgs.log",
  "-serial","file:$out\qemu_vs_tcgs.log",
  "-trace","events=$ev","-D","$out\qemu_tr_tcgs.log")

$proc = Start-Process -FilePath $q -ArgumentList $a -WindowStyle Hidden -PassThru
Write-Output ("PID=" + $proc.Id)
Start-Sleep -Seconds 45
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Output "killed" } else { Write-Output ("exited=" + $proc.ExitCode) }
