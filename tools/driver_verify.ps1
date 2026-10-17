# nefuOS hardware driver verification v2 (QEMU 11: -device AC97 / intel-hda + wav backend).
# Boots dist/nefuOS.iso under several audio/NIC combinations, greps the serial
# log for driver bring-up markers and checks the wav backend received audio.
# Usage:  powershell -ExecutionPolicy Bypass -File tools\driver_verify.ps1 [-Seconds 22]
param(
    [int]$Seconds = 22,
    [string]$Root = "D:\mycppos1\nefuOS"
)
$ErrorActionPreference = "Stop"
Set-Location $Root

$qemu = "D:\qemu\qemu-system-x86_64.exe"
if (-not (Test-Path $qemu)) { $qemu = Join-Path $Root "tools\qemu\qemu-system-x86_64.exe" }
$iso  = Join-Path $Root "dist\nefuOS.iso"
if (-not (Test-Path $iso)) { throw "missing $iso (build first)" }

$logDir = Join-Path $Root "build\driver_logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$combos = @(
    @{ Name = "ac97+e1000";
       Args = @("-device", "AC97", "-nic", "model=e1000");
       Want = @("AC'97 controller ready", "engine ready", "probing Intel PRO/1000", "Intel PRO/1000 (e1000) up");
       Wav  = "" },
    @{ Name = "ac97+rtl8139";
       Args = @("-device", "AC97", "-nic", "model=rtl8139");
       Want = @("AC'97 controller ready", "engine ready", "probing Realtek RTL8139", "Realtek RTL8139 up");
       Wav  = "" },
    @{ Name = "hda+virtio";
       Args = @("-device", "intel-hda", "-device", "hda-output,audiodev=a0,cad=0",
                "-audiodev", "wav,id=a0,path=$logDir\hda_out.wav", "-nic", "model=virtio-net-pci");
       Want = @("Intel HD Audio ready", "engine ready", "probing Virtio Network Device", "Virtio Network Device up");
       Wav  = "hda_out.wav" },
    @{ Name = "nosound+e1000";
       Args = @("-nic", "model=e1000");
       Want = @("no PCI sound card (falling back to SB16 probe)", "probing Intel PRO/1000");
       Wav  = "" }
)

$overall = 0
foreach ($c in $combos) {
    $log = Join-Path $logDir ($c.Name + ".log")
    if (Test-Path $log) { Remove-Item $log -Force }
    if ($c.Wav) { $wavPath = Join-Path $logDir $c.Wav; if (Test-Path $wavPath) { Remove-Item $wavPath -Force } }
    Write-Host ("[verify] " + $c.Name + "  (" + $Seconds + "s) ...")
    $p = Start-Process -FilePath $qemu -ArgumentList (@(
        "-m", "256M",
        "-cdrom", $iso,
        "-boot", "d",
        "-serial", "file:$log",
        "-display", "none",
        "-no-reboot"
    ) + $c.Args) -PassThru -NoNewWindow
    Start-Sleep -Seconds $Seconds
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Seconds 1

    if (-not (Test-Path $log)) { Write-Host "  [FAIL] no serial log"; $overall++; continue }
    $text = Get-Content $log -Raw -Encoding UTF8
    $bad = 0
    foreach ($w in $c.Want) {
        if ($text.Contains($w)) { Write-Host ("  [ok]   " + $w) }
        else { Write-Host ("  [FAIL] missing: " + $w); $bad++ }
    }
    if ($c.Wav -and (Test-Path (Join-Path $logDir $c.Wav))) {
        $len = (Get-Item (Join-Path $logDir $c.Wav)).Length
        if ($len -gt 44) { Write-Host ("  [ok]   audio wav capture: " + $len + " bytes") }
        else { Write-Host ("  [FAIL] wav capture empty (" + $len + " bytes)"); $bad++ }
    }
    if ($bad -eq 0) { Write-Host ("  RESULT: PASS - " + $c.Name) }
    else { Write-Host ("  RESULT: FAIL - " + $c.Name); $overall++ }
}

Write-Host "================================"
if ($overall -eq 0) { Write-Host "DRIVER VERIFY: ALL PASS"; exit 0 }
Write-Host "DRIVER VERIFY: $overall combo(s) failed (see build\driver_logs\)"; exit 1
