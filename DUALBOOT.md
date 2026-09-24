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
2. The boot picker probes the primary hard disk (int 13h AH=15 + AH=42
   extended read of the MBR, signature 0xAA55 + non-empty partition table):
   - **no bootable second OS detected** -> boots nefuOS immediately (no menu).
   - **second OS detected** -> an 80x25 blue "nefuOS Boot Manager" page
     appears with `nefuOS (CD)` and `Windows (Hard Disk)`:
     * UP/DOWN moves the highlight, ENTER confirms.
     * after 3 seconds the highlighted choice starts automatically
       (default highlight = nefuOS, so timeout always boots nefuOS).
3. Choosing `Windows (Hard Disk)` issues `int 19h` (warm reboot); the BIOS
   then boots the hard disk (Windows). Choosing `nefuOS (CD)` returns to the
   CD loader and boots nefuOS.

## Boot chain (ISO, El Torito no-emulation)
```
SeaBIOS -> 2 KiB boot image @0x7C00 (boot.s, LBA 23)
boot.s -> loads menu.bin (2 CD sectors = 4 KiB) @0x10000 (LBA 24-25),
           ljmp 0x1000:0x0000 -> menu.s (multi-boot picker)
menu.s  -> detects 2nd OS on HD; single boot: ljmp back to boot.s cd_load_kernel;
           multi boot: blue picker page -> choice (W = int19 to HD / N = nefuOS)
boot.s  -> cd_load_kernel: loads kernel payload from LBA 26 onward to 0x20000
           (two-stage: 0x20000-0xB0000, then 0x13000 for the remainder)
        -> protected mode -> kernel entry (entry.s) -> long mode -> kernel main
```

## Notes
- The Windows->nefuOS loader is a raw 512-byte ATA-PIO FAT32 reader
  (`backends\bare\boot_hd.s`). It reads `KERNEL.BIN` by cluster chain to
  0x20000 and jumps into the nefuOS kernel in protected mode.
- The picker (`backends\bare\menu.s`) only inspects the MBR signature and
  partition table; it does not touch the Windows disk contents.
- Choosing Windows from the picker just issues `int 19h`; the real Windows
  boot is entirely handled by the machine BIOS + Windows BCD, which is why
  `tools\install_dualboot.ps1` and the BCD entry stay unchanged.
- It does not modify the Windows registry or any Windows system file.
- QEMU v11 note: under QEMU, SeaBIOS + `smm=off` has an IDE emulation quirk
  that can stall raw PIO ports after the MBR load; real machine BIOS behaves
  correctly. The ISO path (CD boot) is fully verified under QEMU.
- If bcdedit fails with "Access is denied", re-run the script from an
  elevated (Administrator) PowerShell.
