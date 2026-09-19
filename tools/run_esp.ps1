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
# NOTE: use the classic full-image OVMF (OVMF.legacy.fd).  The 4M split
# variant (OVMF_CODE.4m.fd / OVMF_VARS.4m.fd) makes the GRUB multiboot2
# relocator #PF (its destination page is mapped read-only); the legacy
# firmware loads the kernel cleanly.
$legacyFd = "tools\grub_toolchain\ovmf_legacy_x\usr\share\ovmf\OVMF.legacy.fd"
if (-not (Test-Path $legacyFd)) { throw "OVMF.legacy.fd missing (run tools\fetch_grub_toolchain.py)" }
$fwTmp = Join-Path $env:TEMP "nefu_ovmf_legacy.fd"
Copy-Item $legacyFd $fwTmp -Force

if (-not (Test-Path $Img)) { throw "image not found: $Img" }

$args = @(
    "-m", "128M",
    "-drive", "if=pflash,format=raw,file=$fwTmp",
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
