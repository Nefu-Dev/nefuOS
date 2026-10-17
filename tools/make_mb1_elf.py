# -*- coding: utf-8 -*-
"""Build a multiboot1 (v1) ELF around the nefuOS kernel so that
``qemu-system-x86_64 -kernel kernel.elf`` boots it directly.

QEMU 11.1's -kernel only implements multiboot v1 (0x1BADB002), not v2.
The nefuOS kernel itself carries a multiboot2 header (for GRUB); to boot it
with QEMU -kernel we prepend a tiny build-time shim (tools/shim_mb1.s):

  ELF
    PT_LOAD[0]  shim         @0x10000  (multiboot1 hdr + code)
    PT_LOAD[1]  kernel.bin   @0x220000 (flat kernel, link base 0x100000)
  e_entry = 0x1000C (shim, after the 12-byte multiboot1 header)

The shim programs bochs VBE 1024x768x32, writes bootinfo to 0x7000, copies
the kernel from 0x220000 up to 0x100000 (no overlap), then jumps to
kernel_start (0x100068) with EAX=0 so entry.s takes its legacy bootinfo path.

Usage:
    python make_mb1_elf.py <kernel.bin> <shim.s> <out.elf> <as> <objcopy>
      kernel.bin: flat kernel image (rebase to 0x100000 first)
      shim.s:     tools/shim_mb1.s
      out.elf:    output multiboot1 ELF
      as, objcopy: MinGW tool paths
"""
import os
import struct
import subprocess
import sys

SHIM_BASE = 0x340000      # where the shim runs (free RAM above kernel+park,
                           # below kalloc heap 0x400000); low memory 0x10000 is
                           # where QEMU drops the multiboot info struct, so it
                           # would clobber the shim there
SHIM_ENTRY = SHIM_BASE + 12  # after the 12-byte multiboot1 header
KERN_SRC = 0x220000        # where kernel.bin is parked
KERN_DST = 0x100000        # kernel link base
KERN_ENTRY = KERN_DST + 0x68  # kernel_start offset within the flat kernel
MB1_MAGIC = 0x1BADB002
MB1_FLAGS = 0x2            # bit1 = memory info (no video; shim does its own VBE)


def assemble(shim_s, out_o, out_bin, as_tool, objcopy):
    r = subprocess.run([as_tool, "--32", shim_s, "-o", out_o],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("as shim failed:\n" + r.stderr)
    r = subprocess.run([objcopy, "-O", "binary", "-j", ".text", out_o, out_bin],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("objcopy shim failed:\n" + r.stderr)
    return open(out_bin, "rb").read()


def patch_size(shim, ksize):
    """Patch the `movl $0, %ecx` placeholder (B9 00000000) with the dword count."""
    b = bytearray(shim)
    pat = b"\xb9\x00\x00\x00\x00"          # movl $imm32, %ecx
    i = b.find(pat)
    if i < 0:
        raise RuntimeError("cannot find shim size placeholder")
    struct.pack_into("<I", b, i + 1, ksize // 4)
    return bytes(b)


def main():
    if len(sys.argv) != 6:
        print("usage: make_mb1_elf.py <kernel.bin> <shim.s> <out.elf> <as> <objcopy>")
        return 2
    kernel_path, shim_s, out_path, as_tool, objcopy = sys.argv[1:6]

    kernel = open(kernel_path, "rb").read()
    ksize = len(kernel)

    shim = assemble(shim_s, shim_s + ".o", shim_s + ".bin", as_tool, objcopy)
    shim = patch_size(shim, ksize)

    # multiboot1 header (12 bytes) prepended to the shim code
    checksum = -(MB1_MAGIC + MB1_FLAGS) & 0xFFFFFFFF
    hdr = struct.pack("<III", MB1_MAGIC, MB1_FLAGS, checksum)
    shim_blob = hdr + shim

    # ---- ELF64, two PT_LOADs ----
    ehsize, phentsize = 64, 56
    phnum = 2
    eh = bytearray(ehsize)
    eh[0:4] = b"\x7fELF"; eh[4] = 2; eh[5] = 1; eh[6] = 1
    struct.pack_into("<H", eh, 16, 2)            # ET_EXEC
    struct.pack_into("<H", eh, 18, 3)            # EM_386: QEMU multiboot1 rejects x86-64
                                                # images ("give a 32bit one"); the shim
                                                # entry is 32-bit, kernel long-modes itself
    struct.pack_into("<I", eh, 20, 1)
    struct.pack_into("<Q", eh, 24, SHIM_ENTRY)   # e_entry
    struct.pack_into("<Q", eh, 32, ehsize)       # e_phoff
    struct.pack_into("<Q", eh, 40, 0)
    struct.pack_into("<I", eh, 48, 0)
    struct.pack_into("<H", eh, 52, ehsize)
    struct.pack_into("<H", eh, 54, phentsize)
    struct.pack_into("<H", eh, 56, phnum)

    shim_off = ehsize + phentsize * phnum
    kern_off = shim_off + len(shim_blob)

    def ph(off, vaddr, filesz, p_flags):
        b = bytearray(phentsize)
        struct.pack_into("<I", b, 0, 1)           # PT_LOAD
        struct.pack_into("<I", b, 4, p_flags)     # R|W|X
        struct.pack_into("<Q", b, 8, off)
        struct.pack_into("<Q", b, 16, vaddr)
        struct.pack_into("<Q", b, 24, vaddr)
        struct.pack_into("<Q", b, 32, filesz)
        struct.pack_into("<Q", b, 40, filesz)
        struct.pack_into("<Q", b, 48, 0x1000)
        return b

    phs = ph(shim_off, SHIM_BASE, len(shim_blob), 7) + \
          ph(kern_off, KERN_SRC, ksize, 7)

    with open(out_path, "wb") as f:
        f.write(eh); f.write(phs); f.write(shim_blob); f.write(kernel)

    print("MB1 ELF OK: %s (%d bytes) shim@0x%X entry 0x%X kern@0x%X (0x%X bytes)"
          % (out_path, os.path.getsize(out_path), SHIM_BASE, SHIM_ENTRY,
             KERN_SRC, ksize))
    return 0


if __name__ == "__main__":
    sys.exit(main())
