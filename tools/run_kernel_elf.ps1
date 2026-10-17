# Boot the nefuOS ELF kernel directly in QEMU.
# Usage:  powershell -ExecutionPolicy Bypass -File tools\run_kernel_elf.ps1
$root = Split-Path -Parent $PSScriptRoot
$qemu = Join-Path $root "tools\qemu\qemu-system-x86_64.exe"
$kern = Join-Path $root "dist\nefuOS_kernel.elf"
if (-not (Test-Path $qemu)) { throw "QEMU not found: $qemu" }
if (-not (Test-Path $kern)) { throw "ELF kernel not found: $kern" }
Write-Host "Booting nefuOS ELF kernel directly: qemu -kernel $kern"
& $qemu -kernel $kern -m 128M -vga std
