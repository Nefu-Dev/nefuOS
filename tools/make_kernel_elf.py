# -*- coding: utf-8 -*-
"""Wrap the nefuOS bare kernel into a genuine ELF64 image that can be booted
directly (``qemu-system-x86_64 -kernel kernel.elf``, GRUB multiboot2, ...).

The kernel is compiled/linked by MinGW into a flat image (kernel.bin) with an
embedded Multiboot2 header in backends/bare/entry.s.  This tool:

  1) rebases kernel.bin from its original PE link base (0x20000) to the
     Multiboot2 load base 0x100000 (reusing tools/rebase_kernel.py), and
  2) wraps the rebased flat image into a valid ELF64 executable:

       ELF header        @ file 0x00  (64 B)
       program header    @ file 0x40  (56 B)
       kernel image      @ file 0x78  (maps to 0x100000 via PT_LOAD offset)
         Multiboot2 hdr  @ file 0x80  -> runtime 0x100008 (magic 0xE85250D6)
         kernel_start    @ file 0xE0  -> runtime 0x100068 (multiboot2 entry)

The single PT_LOAD segment (RWX, p_vaddr/p_paddr = 0x100000, p_offset = 0x78)
keeps the in-image Multiboot2 Address tag (load_addr=0x100000, header_addr=
0x100008, entry=0x100068) consistent, so both GRUB2 and QEMU's multiboot2
loader map the image at 0x100000 and jump to kernel_start (32-bit PM entry).

Usage:
    python make_kernel_elf.py <kernel.bin> <kernel.exe> <out.elf> [<base>]
      kernel.bin: flat image produced by Rebin-Kernel (PE base 0x20000)
      kernel.exe: the PE used to build it (carries the .reloc fixups)
      out.elf:    output ELF64 kernel path
      base:       Multiboot2 load base (default 0x100000)
"""
import struct
import subprocess
import sys
import os
import tempfile

LOAD_BASE = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x100000
ENTRY_OFF = 0x68            # kernel_start offset within the flat image
HDR_OFF = 0x08              # Multiboot2 header offset within the flat image
E_ENTRY = LOAD_BASE + ENTRY_OFF

MB2_MAGIC = 0xE85250D6


def rebase(bin_path, pe_path, out_path):
    """Rebase the flat kernel from 0x20000 to LOAD_BASE via rebase_kernel.py."""
    py = sys.executable
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "rebase_kernel.py")
    r = subprocess.run([py, script, bin_path, pe_path, hex(LOAD_BASE),
                        out_path], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise RuntimeError("rebase_kernel.py failed (rc=%d)" % r.returncode)
    return out_path


def make_elf(flat, out_path):
    """Build a genuine ELF64 (ET_EXEC, EM_X86_64) around the flat image."""
    data = bytearray(flat)
    size = len(data)

    # sanity: Multiboot2 header must sit at the expected in-image offset.
    if len(data) <= HDR_OFF + 4:
        raise RuntimeError("kernel image too small")
    if struct.unpack_from("<I", data, HDR_OFF)[0] != MB2_MAGIC:
        raise RuntimeError("multiboot2 magic (0x%08X) not at offset 0x%x"
                           % (MB2_MAGIC, HDR_OFF))

    ehsize = 64
    phentsize = 56
    phnum = 1
    phoff = ehsize
    data_off = ehsize + phentsize * phnum   # kernel image starts after ELF + PH

    # ---- ELF header ----
    eh = bytearray(ehsize)
    eh[0:4] = b"\x7fELF"
    eh[4] = 2                 # EI_CLASS = ELFCLASS64
    eh[5] = 1                 # EI_DATA = ELFDATA2LSB
    eh[6] = 1                 # EI_VERSION
    eh[7] = 0                 # EI_OSABI (System V)
    struct.pack_into("<H", eh, 16, 2)      # e_type = ET_EXEC
    struct.pack_into("<H", eh, 18, 62)     # e_machine = EM_X86_64
    struct.pack_into("<I", eh, 20, 1)      # e_version
    struct.pack_into("<Q", eh, 24, E_ENTRY)
    struct.pack_into("<Q", eh, 32, phoff)  # e_phoff
    struct.pack_into("<Q", eh, 40, 0)      # e_shoff
    struct.pack_into("<I", eh, 48, 0)      # e_flags
    struct.pack_into("<H", eh, 52, ehsize) # e_ehsize
    struct.pack_into("<H", eh, 54, phentsize)
    struct.pack_into("<H", eh, 56, phnum)
    struct.pack_into("<H", eh, 58, 0)      # e_shentsize
    struct.pack_into("<H", eh, 60, 0)      # e_shnum
    struct.pack_into("<H", eh, 62, 0)      # e_shstrndx

    # ---- program header: one RWX PT_LOAD mapping the flat image at 0x100000 ----
    ph = bytearray(phentsize)
    struct.pack_into("<I", ph, 0, 1)              # p_type = PT_LOAD
    struct.pack_into("<I", ph, 4, 7)              # p_flags = R|W|X
    struct.pack_into("<Q", ph, 8, data_off)       # p_offset = 0x78
    struct.pack_into("<Q", ph, 16, LOAD_BASE)     # p_vaddr
    struct.pack_into("<Q", ph, 24, LOAD_BASE)     # p_paddr
    struct.pack_into("<Q", ph, 32, size)          # p_filesz
    struct.pack_into("<Q", ph, 40, size)          # p_memsz (no .bss; entry.s zeroes it)
    struct.pack_into("<Q", ph, 48, 0x1000)        # p_align

    out = bytearray(eh) + bytearray(ph) + data
    with open(out_path, "wb") as f:
        f.write(out)
    return len(out)


def main():
    if len(sys.argv) not in (4, 5):
        print("usage: make_kernel_elf.py <kernel.bin> <kernel.exe> <out.elf> [<base>]")
        return 2
    bin_path, pe_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    fd, tmp = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    try:
        rebase(bin_path, pe_path, tmp)
        flat = open(tmp, "rb").read()
    finally:
        os.remove(tmp)

    n = make_elf(flat, out_path)
    print("ELF OK: %s (%d bytes), entry 0x%X, load 0x%X" %
          (out_path, n, E_ENTRY, LOAD_BASE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
