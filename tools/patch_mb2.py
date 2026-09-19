# -*- coding: utf-8 -*-
"""Patch the multiboot2 header of nefuOS kernel.bin.

The header (entry.s, offset 8 in the flat image) carries an Address tag whose
load_end_addr / bss_end_addr must match the built PE layout, plus an in-kernel
kernel_boot_params block that entry.s reads to zero-fill .bss on BOTH boot
paths (legacy ISO and GRUB/ESP).  All values are computed by build_iso.ps1
from the PE section table (Re bin-Kernel) and passed on the command line.

Usage:
    python patch_mb2.py <kernel.bin> <bss_start> <bss_size> [<out.bin>]

    bss_start / bss_size: decimal or 0x-prefixed; bss_start is the ABSOLUTE
    runtime address (0x20000 + RVA).  If <out.bin> is omitted, kernel.bin is
    patched in place.

Layout of the header (see backends/bare/entry.s):
    +0x08 magic 0xE85250D6
    +0x10 framebuffer tag (type 5)
    +0x30 address tag (type 2): header_addr/load_addr/load_end_addr/bss_end_addr
    +0x48 entry address tag (type 3)
    +0x58 end tag (type 0)
    +0x60 kernel_boot_params: bss_start, bss_size
The tool locates everything by walking the tags (robust to layout drift).
"""
import struct
import sys

LOAD_BASE = 0x20000
MAGIC = 0xE85250D6
SCAN_LIMIT = 32768  # multiboot2 spec: header within the first 32 KiB


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def find_header(data):
    for off in range(0, SCAN_LIMIT - 8, 8):
        if u32(data, off) == MAGIC:
            arch = u32(data, off + 4)
            length = u32(data, off + 8)
            cksum = u32(data, off + 12)
            if arch == 0 and length >= 16 and (MAGIC + arch + length + cksum) & 0xFFFFFFFF == 0:
                return off, length
    return None, None


def walk_tags(data, hdr):
    """Yield (type, size, off) for each tag starting right after the base header."""
    off = hdr + 16
    while off + 8 <= len(data):
        t = u32(data, off)
        size = u32(data, off + 4)
        if size < 8 or off + size > len(data):
            break
        yield t, size, off
        if t == 0:
            break
        off = off + size
        off = (off + 7) & ~7
    return


def main():
    if len(sys.argv) not in (4, 5):
        print("usage: patch_mb2.py <kernel.bin> <bss_start> <bss_size> [<out.bin>]")
        return 2
    path = sys.argv[1]
    bss_start = int(sys.argv[2], 0)
    bss_size = int(sys.argv[3], 0)
    out_path = sys.argv[4] if len(sys.argv) == 5 else path

    data = bytearray(open(path, "rb").read())
    hdr, hlen = find_header(data)
    if hdr is None:
        print("ERROR: multiboot2 header not found in first %d bytes" % SCAN_LIMIT)
        return 1
    print("multiboot2 header at file offset 0x%x, length %d" % (hdr, hlen))

    file_size = len(data)
    load_end = LOAD_BASE + file_size
    # multiboot2 Address tag: GRUB zero-fills [load_end_addr, bss_end_addr).
    # The toolchain puts ALL static state in .data (PE: no .bss section), so
    # bss_size is normally 0 -> clamp bss_end to load_end (zero-fill nothing).
    bss_end = max(load_end, bss_start + bss_size)
    if bss_start and bss_start < load_end:
        print("WARN: bss_start (0x%x) < load_end (0x%x); GRUB zero-fill range will overlap file data"
              % (bss_start, load_end))
    if bss_end > 0x100000000:
        print("ERROR: bss_end 0x%x above 4 GiB (multiboot2 Address tag is 32-bit)" % bss_end)
        return 1

    addr_tag = None
    for t, size, off in walk_tags(data, hdr):
        if t == 2:
            addr_tag = off
            break
    if addr_tag is None:
        print("ERROR: Address tag (type 2) missing")
        return 1
    # Address tag layout: +0 type +4 size +8 header_addr +12 load_addr
    #                          +16 load_end_addr +20 bss_end_addr
    hdr_addr = u32(data, addr_tag + 8)
    load_addr = u32(data, addr_tag + 12)
    print("  address tag @0x%x: header_addr=0x%x load_addr=0x%x" % (addr_tag, hdr_addr, load_addr))
    if load_addr != LOAD_BASE:
        print("ERROR: load_addr 0x%x != 0x20000" % load_addr)
        return 1
    struct.pack_into("<I", data, addr_tag + 16, load_end)
    struct.pack_into("<I", data, addr_tag + 20, bss_end)

    # kernel_boot_params sits 8-aligned right after the end tag
    boot_params = None
    for t, size, off in walk_tags(data, hdr):
        if t == 0:
            boot_params = (off + size + 7) & ~7
            break
    if boot_params is None or boot_params + 8 > len(data):
        print("ERROR: cannot locate kernel_boot_params after end tag")
        return 1
    struct.pack_into("<I", data, boot_params, bss_start)
    struct.pack_into("<I", data, boot_params + 4, bss_size)

    open(out_path, "wb").write(data)
    print("patched: load_end_addr=0x%x bss_end_addr=0x%x" % (load_end, bss_end))
    print("patched: kernel_boot_params@0x%x bss_start=0x%x bss_size=0x%x (%d bytes)"
          % (boot_params, bss_start, bss_size, bss_size))
    print("OK: %s (%d bytes)" % (out_path, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
