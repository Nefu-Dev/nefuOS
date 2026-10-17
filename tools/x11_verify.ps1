# nefuOS X11 / Electron verification (headless QEMU, COM1/serial).
# Boots the bare kernel (which runs the baked [XREQ] selftest), then greps the
# serial log for: X server init -> guest socket connect -> setup handshake ->
# CreateWindow/CreateGC/MapWindow -> Expose -> draw batch -> pixel assertions ->
# SELFTEST PASS.
param(
    [int]$Seconds = 280,
    [string]$Root = "D:\mycppos1\nefuOS",
    [string]$Iso = ""
)
$ErrorActionPreference = "Stop"
Set-Location $Root

$qemu = "D:\qemu\qemu-system-x86_64.exe"
if (-not (Test-Path $qemu)) { $qemu = Join-Path $Root "tools\qemu\qemu-system-x86_64.exe" }
if ($Iso -eq "") { $iso = Join-Path $Root "dist\nefuOS.iso" } else { $iso = $Iso }
$disk = Join-Path $Root "build\samples\deb_test.img"
$log  = Join-Path $Root "build\samples\x11_serial.log"
if (-not (Test-Path $iso)) { throw "missing $iso (build first)" }
if (Test-Path $log) { Remove-Item $log -Force }

Write-Host "[x11-verify] booting headless QEMU ($Seconds s) ..."
$p = Start-Process -FilePath $qemu -ArgumentList @(
    "-m", "192M",
    "-accel", "tcg,tb-size=128",
    "-hda", $disk,
    "-cdrom", $iso,
    "-boot", "d",
    "-serial", "file:$log",
    "-display", "none",
    "-no-reboot"
) -PassThru -NoNewWindow
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 1

if (-not (Test-Path $log)) { throw "no serial log produced" }
$text = Get-Content $log -Raw -Encoding UTF8
Write-Host "---- serial log (${Seconds}s) ----"
Write-Host $text
Write-Host "----------------------------------"

$checks = @(
    "[XREQ] nefuOS embedded X11 server selftest begin",
    "[XREQ] baked client ELF size=",
    "[XREQ] run guest X client",
    "x11 client drew",             # guest marker; "ok\n" may interleave with [T] trace output
    "[XREQ] guest exited",
    "[XREQ] req_count=",
    "draw_ops=",
    "windows=2",
    "[XREQ] pixel rect=ffffff bg=0",   # xb_puthex prints no 0x prefix
    "[XREQ] SELFTEST PASS",
    "[XREQ] electron wins=",
    "[XREQ] ELECTRON PASS"
)
$fail = 0
foreach ($c in $checks) {
    if ($text.Contains($c)) { Write-Host "[ok] $c" }
    else { Write-Host "[FAIL] missing: $c"; $fail++ }
}
if ($fail -eq 0) { Write-Host "RESULT: PASS - embedded X11 server + guest renderer verified" }
else { Write-Host "RESULT: FAIL - $fail marker(s) missing"; exit 1 }
