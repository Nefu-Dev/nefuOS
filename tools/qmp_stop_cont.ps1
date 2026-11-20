$ErrorActionPreference = 'Continue'
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$iso = Join-Path $env:TEMP "nefu_dist\nefuOS.iso"
$out = "D:\mycppos1\nefuOS\dist"
$q   = "D:\mycppos1\nefuOS\tools\qemu\qemu-system-x86_64.exe"
$ev  = Join-Path $out "trace_ev_ok.txt"
if (-not (Test-Path -LiteralPath $ev)) { Write-Output "missing trace events file"; exit 1 }

foreach ($f in @("qemu_vg_stop.log","qemu_vs_stop.log","qemu_tr_stop.log")) {
  $p = Join-Path $out $f
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}

$a = @("-accel","whpx","-m","256M",
  "-drive","file=$iso,media=cdrom,format=raw","-boot","d",
  "-device","virtio-gpu","-vga","none","-display","none",
  "-debugcon","file:$out\qemu_vg_stop.log",
  "-serial","file:$out\qemu_vs_stop.log",
  "-qmp","tcp:127.0.0.1:45461,server,nowait",
  "-trace","events=$ev","-D","$out\qemu_tr_stop.log")

$proc = Start-Process -FilePath $q -ArgumentList $a -WindowStyle Hidden -PassThru
Write-Output ("PID=" + $proc.Id)
Start-Sleep -Seconds 9

$sock = New-Object System.Net.Sockets.TcpClient
$sock.Connect("127.0.0.1",45461)
$stream = $sock.GetStream()
$reader = New-Object System.IO.StreamReader($stream)
$writer = New-Object System.IO.StreamWriter($stream)
$writer.AutoFlush = $true
Start-Sleep -Milliseconds 900

# greeting
$banner = $reader.ReadLine()
Write-Output ("banner: " + $banner)
$writer.WriteLine('{"execute":"qmp_capabilities"}')
Start-Sleep -Milliseconds 500
Write-Output ("caps: " + $reader.ReadLine())

$writer.WriteLine('{"execute":"stop"}')
Start-Sleep -Milliseconds 600
Write-Output ("stop: " + $reader.ReadLine())

Start-Sleep -Seconds 6

$writer.WriteLine('{"execute":"query-status"}')
Start-Sleep -Milliseconds 600
Write-Output ("status-while-stopped: " + $reader.ReadLine())

$writer.WriteLine('{"execute":"cont"}')
Start-Sleep -Milliseconds 600
Write-Output ("cont: " + $reader.ReadLine())

# queue status right after cont
Start-Sleep -Seconds 2
$writer.WriteLine('{"execute":"x-query-virtio-queue-status","arguments":{"path":"/machine/peripheral-anon/device[0]/virtio-backend","queue":0}}')
Start-Sleep -Milliseconds 700
Write-Output ("qstatus: " + $reader.ReadLine())

$sock.Close()
Start-Sleep -Seconds 15
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Output "killed" } else { Write-Output ("exited=" + $proc.ExitCode) }
