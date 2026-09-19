# nefuOS ESP+GRUB boot test — QEMU with OVMF (UEFI firmware).
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\run_esp.ps1            # GUI window
#   powershell -ExecutionPolicy Bypass -File tools\run_esp.ps1 -NoDisplay # headless (logs only)
param(
    [string]$Img = "dist\nefuOS_esp.img",
    [switch]$NoDisplay
)
$ErrorActionPreference = "Stop"
$root = "D:\mycppos1\nefuOS"
Set-Location $root

# ---- locate QEMU ----
$qemu = $null
foreach ($p in @("D:\mycppos1\nefuOS\tools\qemu\qemu-system-x86_64.exe",
                 "D:\andr-comston\emulator\qemu\windows-x86_64\qemu-system-x86_64.exe",
                 "C:\Program Files\qemu\qemu-system-x86_64.exe",
                 "C:\Program Files\qemu\qemu-system-x86_64-headless.exe")) {
    if (Test-Path $p) { $qemu = $p; break }
}
if (-not $qemu) {
    $g = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($g) { $qemu = $g.Source }
}
if (-not $qemu) { throw "qemu-system-x86_64 not found" }
Write-Output "QEMU: $qemu"

# ---- OVMF firmware ----
$ovmfDir = "tools\grub_toolchain\ovmf"
if (-not (Test-Path "$ovmfDir\OVMF_CODE.4m.fd")) { throw "OVMF_CODE.4m.fd missing (run tools\fetch_grub_toolchain.py)" }
$varsTmp = Join-Path $env:TEMP "nefu_ovmf_vars.fd"
Copy-Item "$ovmfDir\OVMF_VARS.4m.fd" $varsTmp -Force

if (-not (Test-Path $Img)) { throw "image not found: $Img" }

$args = @(
    "-m", "128M",
    "-drive", "if=pflash,format=raw,readonly=on,file=$ovmfDir\OVMF_CODE.4m.fd",
    "-drive", "if=pflash,format=raw,file=$varsTmp",
    "-drive", "file=$Img,format=raw,if=ide",
    "-net", "nic,model=e1000",
    "-net", "user",
    "-device", "isa-debugcon,iobase=0xE9,chardev=dbg",
    "-chardev", "file,id=dbg,path=debug.log",
    "-serial", "file:serial.log"
)
if ($NoDisplay) { $args += @("-display", "none") }
Write-Output "booting nefuOS via ESP + GRUB under OVMF ..."
Write-Output ("debugcon -> debug.log   serial -> serial.log")
& $qemu @args
