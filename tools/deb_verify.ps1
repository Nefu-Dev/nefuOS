# nefuOS .deb environment verification (headless QEMU, COM1/serial).
# Builds nothing; expects build/iso (dist/nefuOS.iso) + build/samples/deb_test.img.
# Runs the bare kernel for a bounded time, then greps the serial log for the
# [DEBREQ] chain: probe -> install -> run -> "Hello from Linux ELF in nefuOS!".
param(
    [int]$Seconds = 75,
    [string]$Root = "D:\mycppos1\nefuOS"
)
$ErrorActionPreference = "Stop"
Set-Location $Root

$qemu = "D:\qemu\qemu-system-x86_64.exe"
if (-not (Test-Path $qemu)) { $qemu = Join-Path $Root "tools\qemu\qemu-system-x86_64.exe" }
$iso  = Join-Path $Root "dist\nefuOS.iso"
$disk = Join-Path $Root "build\samples\deb_test.img"
$log  = Join-Path $Root "build\samples\deb_serial.log"
if (-not (Test-Path $iso)) { throw "missing $iso (build first)" }
if (-not (Test-Path $disk)) { throw "missing $disk (run tools/mk_deb_env.py)" }
if (Test-Path $log) { Remove-Item $log -Force }

Write-Host "[deb-verify] booting headless QEMU ($Seconds s) ..."
$p = Start-Process -FilePath $qemu -ArgumentList @(
    "-m", "256M",
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
    "[DEBREQ] nefuOS .deb environment selftest begin",
    "[DEBREQ] probe ok:",
    "format: Debian .deb (ar)",
    "Package: nefu-hello",
    "[DEBREQ] install ->",
    "installed 2 file(s) from data.tar to VFS",
    "[DEBREQ] elf path: /usr/bin/hello",
    "[DEBREQ] run entry=0x7800000",
    "Hello from Linux ELF in nefuOS!",
    "[DEBREQ] run done"
)
$fail = 0
foreach ($c in $checks) {
    if ($text.Contains($c)) { Write-Host "[ok] $c" }
    else { Write-Host "[FAIL] missing: $c"; $fail++ }
}
if ($fail -eq 0) { Write-Host "RESULT: PASS - .deb probe/install/run verified" }
else { Write-Host "RESULT: FAIL - $fail marker(s) missing"; exit 1 }
