# nefuOS dual-boot notes

## Windows -> nefuOS
1. Copy `boot_hd.bin` to `C:\nefuOS\` (or keep next to the script).
2. Put the nefuOS kernel on the FIRST FAT32 partition of the boot disk:
   - format a small partition as FAT32, mount it, copy the kernel file as
     `KERNEL.BIN` (8.3 uppercase name, at the partition root).
3. Run as Administrator:
   `powershell -ExecutionPolicy Bypass -File tools\install_dualboot.ps1`
4. Reboot, choose "nefuOS" in the Windows boot menu.

## nefuOS -> Windows
1. Boot the nefuOS ISO in a VM or real machine.
2. Within 2.5 seconds press `W` on the boot menu to warm-reboot into the
   hard disk (Windows). Default boots nefuOS.

## Notes
- The Windows->nefuOS loader is a raw 512-byte ATA-PIO FAT32 reader
  (`backends\bare\boot_hd.s`). It reads `KERNEL.BIN` by cluster chain to
  0x20000 and jumps into the nefuOS kernel in protected mode.
- It does not modify the Windows registry or any Windows system file.
- QEMU v11 note: under QEMU, SeaBIOS + `smm=off` has an IDE emulation quirk
  that can stall raw PIO ports after the MBR load; real machine BIOS behaves
  correctly. The ISO path (CD boot) is fully verified under QEMU.
- If bcdedit fails with "Access is denied", re-run the script from an
  elevated (Administrator) PowerShell.
