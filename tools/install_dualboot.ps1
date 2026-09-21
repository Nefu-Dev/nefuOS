# nefuOS dual-boot installer for Windows (Windows -> nefuOS)
# Run this script as Administrator. It adds a Windows boot menu entry that
# chain-loads the nefuOS hard-disk boot sector (boot_hd.bin) via BCD.
# nefuOS -> Windows is handled by the ISO boot menu (press W within 2.5s).
#
# Requirements:
#   - Windows 10/11, UEFI or legacy BIOS (BCD BOOTSECTOR entry)
#   - boot_hd.bin copied next to this script, or set -BootBin path
#   - nefuOS kernel must exist on the first FAT32 partition of the boot disk
#     as KERNEL.BIN (8.3 name), or the ISO is used for the other direction.
#
# Safety: this only adds a boot entry; it never touches Windows registry
# or system files. Remove the entry with:
#   bcdedit /delete {GUID}

param(
    [string]$BootBin = "$PSScriptRoot\boot_hd.bin"
)

$ErrorActionPreference = 'Stop'

# --- 0. elevation check -----------------------------------------------------
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: run this script as Administrator (bcdedit requires elevation)." -ForegroundColor Red
    exit 1
}

# --- 1. sanity checks -------------------------------------------------------
if (-not (Test-Path $BootBin)) {
    Write-Host "ERROR: boot_hd.bin not found at $BootBin" -ForegroundColor Red
    exit 1
}
$bin = [System.IO.File]::ReadAllBytes($BootBin)
if ($bin.Length -ne 512 -or $bin[510] -ne 0x55 -or $bin[511] -ne 0xAA) {
    Write-Host "ERROR: $BootBin is not a valid 512-byte boot sector." -ForegroundColor Red
    exit 1
}

# --- 2. copy boot sector to a stable location ------------------------------
$targetDir = "$env:SystemDrive\nefuOS"
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
$targetBin = Join-Path $targetDir 'boot_hd.bin'
Copy-Item $BootBin $targetBin -Force
Write-Host "OK: copied boot sector to $targetBin"

# --- 3. create BCD bootsector entry ----------------------------------------
$create = bcdedit /create /application bootsector /d "nefuOS"
Write-Host $create
$guidLine = $create | Select-String -Pattern '\{[0-9a-f-]+\}'
if (-not $guidLine) {
    Write-Host "ERROR: could not parse GUID from bcdedit output." -ForegroundColor Red
    exit 1
}
$guid = $guidLine.Matches[0].Value.Trim('{','}')
Write-Host "OK: entry GUID = {$guid}"

bcdedit /set "{$guid}" device partition=$env:SystemDrive | Out-Null
bcdedit /set "{$guid}" path "\nefuOS\boot_hd.bin" | Out-Null
bcdedit /displayorder "{$guid}" /addlast | Out-Null

Write-Host ""
Write-Host "SUCCESS: nefuOS added to the Windows boot menu." -ForegroundColor Green
Write-Host "Next boot: choose 'nefuOS' in the Windows boot menu."
Write-Host "nefuOS must be on the first partition: copy KERNEL.BIN to the"
Write-Host "root of a FAT32 partition of the boot disk (8.3 name, one file)."
Write-Host ""
Write-Host "To remove:  bcdedit /delete {$guid}"
