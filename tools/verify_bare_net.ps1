# tools/verify_bare_net.ps1
#
# Headless QEMU end-to-end check that the bare-metal kernel can reach the
# real Internet: npkg registry over http:// (slowAES challenge solved) and
# the OTA manifest over https:// (native TLS 1.3). Passes when the serial
# log contains the net-selftest markers.
#
# Usage:  powershell -File tools\verify_bare_net.ps1 [-Quick]
param([switch]$Quick)
$ErrorActionPreference = "Stop"
$qemu = "D:\qemu\qemu-system-x86_64.exe"
$root = "D:\mycppos1\nefuOS"
$iso  = Join-Path $env:TEMP "nefu_dist\nefuOS.iso"
$serial = Join-Path $root "build\samples\bare_net_serial.log"
$pcap   = Join-Path $root "build\samples\bare_net_dump.pcap"
New-Item -ItemType Directory -Force -Path (Split-Path $serial) | Out-Null
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item $serial, $pcap -ErrorAction SilentlyContinue

$bootTimeout = if ($Quick) { 150 } else { 300 }

$q = Start-Process $qemu -ArgumentList @(
    "-m","192M","-accel","tcg,tb-size=128",
    "-cdrom",$iso,"-boot","d",
    "-netdev","user,id=n0","-device","e1000,netdev=n0",
    "-object","filter-dump,id=f0,netdev=n0,file=$pcap",
    "-serial","file:$serial",
    "-display","none","-no-reboot"
) -WindowStyle Hidden -PassThru
Write-Output "QEMU PID: $($q.Id) (waiting up to ${bootTimeout}s for net-selftest)"

$deadline = (Get-Date).AddSeconds($bootTimeout)
$done = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if (Test-Path $serial) {
        $log = Get-Content $serial -Raw
        if ($log -match "net-selftest: done") { $done = $true; break }
        if ($log -match "PANIC|triple fault|Kernel panic") { break }
    }
    if ($q.HasExited) { break }
}
Stop-Process -Id $q.Id -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

"--- serial: net-selftest / net markers ---"
if (Test-Path $serial) {
    Select-String -Path $serial -Pattern "net-selftest|net: " | ForEach-Object { $_.Line }
} else {
    "NO SERIAL LOG"
}

$ok = $done -and (Test-Path $serial) -and
      ((Get-Content $serial -Raw) -match "net-selftest: npkg registry OK") -and
      ((Get-Content $serial -Raw) -match "net-selftest: update manifest OK")
if ($ok) { Write-Output "RESULT: PASS - bare-metal HTTP(S) online" }
else     { Write-Output "RESULT: FAIL - bare-metal network not verified" }
exit $(if ($ok) { 0 } else { 1 })
