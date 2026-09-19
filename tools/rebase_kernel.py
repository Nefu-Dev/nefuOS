# -*- coding: utf-8 -*-
"""Rebase nefuOS kernel.bin from its original link base (0x20000) to a new
load base (default 0x100000) for the UEFI/ESP+GRUB boot path.

Why: GRUB's multiboot2 relocator copies the kernel to the Address-tag
load_addr.  At 0x20000 the 1.1 MB kernel spans GRUB's own page tables
(0x7000-0x9000), the legacy VGA hole (0xA0000-0xC0000) and the option-ROM /
BIOS region (0xC0000-0x100000), which stalls or faults inside the relocator
under OVMF.  Loading at 1 MB keeps the whole image in clean usable RAM.
The legacy ISO path stays untouched at 0x20000.

Usage:
    python rebase_kernel.py <kernel.bin> <kernel.exe> [<new_base>] [<out.bin>]

    kernel.bin: flat image produced by Rebin-Kernel (linked at 0x20000)
    kernel.exe: the PE used to build it (carries the .reloc fixups)
    new_base:   load address for the rebased image (default 0x100000)
    out.bin:    output path (default <kernel.bin>.rebase)
"""
import struct
import sys

OLD_BASE = 0x20000
LOAD_BASE = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x100000


def parse_sections(pe, nsec, sec_off):
    secs = []
    for i in range(nsec):
        off = sec_off + i * 40
        name = pe[off:off + 8].rstrip(b"\0").decode()
        vsz, vaddr, rawsz, rawptr = struct.unpack_from("<IIII", pe, off + 8)
        secs.append((name, vaddr, max(vsz, rawsz), rawptr))
    return secs


def rva_to_file(secs, rva):
    # kernel.bin is laid out by VIRTUAL address (offset == RVA): Rebin-Kernel
    # concatenates section contents at their virtual addresses, dropping the
    # PE file-header gap.  The .reloc records carry RVAs, so the byte to fix
    # sits at the same offset in kernel.bin.
    for name, vaddr, span, rawptr in secs:
        if vaddr <= rva < vaddr + span:
            return rva
    return None


def find_reloc_section(secs):
    for name, vaddr, span, rawptr in secs:
        if name == ".reloc":
            return rawptr, span
    return None, None


def walk_header(data, hdr):
    off = hdr + 16
    while off + 8 <= len(data):
        t = struct.unpack_from("<I", data, off)[0]
        size = struct.unpack_from("<I", data, off + 4)[0]
        if size < 8 or off + size > len(data):
            break
        yield t, size, off
        if t == 0:
            break
        off = off + size
        off = (off + 7) & ~7


def main():
    if len(sys.argv) not in (3, 4, 5):
        print("usage: rebase_kernel.py <kernel.bin> <kernel.exe> [<new_base>] [<out.bin>]")
        return 2
    bin_path, pe_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[4] if len(sys.argv) == 5 else bin_path + ".rebase"

    data = bytearray(open(bin_path, "rb").read())
    pe = open(pe_path, "rb").read()

    # --- locate the multiboot2 header (magic at offset 8, per entry.s) ---
    magic = struct.unpack_from("<I", data, 8)[0]
    if magic != 0xE85250D6:
        print("ERROR: mb2 magic not at offset 8")
        return 1
    hdr = 8
    hlen = struct.unpack_from("<I", data, hdr + 8)[0]

    # --- PE section table / .reloc ---
    e_lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    if pe[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        print("ERROR: bad PE signature")
        return 1
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", pe, coff + 2)[0]
    opt = coff + 20
    magic_pe = struct.unpack_from("<H", pe, opt)[0]
    sec_off = opt + (112 if magic_pe == 0x10B else 240)
    secs = parse_sections(pe, nsec, sec_off)
    reloc_raw, reloc_sz = find_reloc_section(secs)
    if reloc_raw is None:
        print("ERROR: .reloc section missing")
        return 1

    delta = LOAD_BASE - OLD_BASE

    # --- apply fixups ---
    blk = reloc_raw
    end = reloc_raw + reloc_sz
    applied = {"HIGHLOW": 0, "DIR64": 0, "ABS": 0, "other": 0}
    while blk < end:
        page_rva, blksize = struct.unpack_from("<II", pe, blk)
        if blksize < 8 or blk + blksize > end:
            print("WARN: truncated reloc block at 0x%x (size %d)" % (blk, blksize))
            break
        n = (blksize - 8) // 2
        for i in range(n):
            w = struct.unpack_from("<H", pe, blk + 8 + i * 2)[0]
            typ = w >> 12
            off = w & 0xFFF
            f = rva_to_file(secs, page_rva + off)
            if f is None or f + 8 > len(data):
                continue
            if typ == 3:  # IMAGE_REL_BASED_HIGHLOW
                v = struct.unpack_from("<I", data, f)[0]
                struct.pack_into("<I", data, f, v + delta)
                applied["HIGHLOW"] += 1
            elif typ == 10:  # IMAGE_REL_BASED_DIR64
                v = struct.unpack_from("<Q", data, f)[0]
                struct.pack_into("<Q", data, f, v + delta)
                applied["DIR64"] += 1
            elif typ == 0:  # ABS
                applied["ABS"] += 1
            else:
                applied["other"] += 1
        blk += blksize
    print("fixups applied: %s" % applied)

    # --- rewrite the Address tag (type 2) + Entry tag (type 3) ---
    file_size = len(data)
    load_end = LOAD_BASE + file_size
    addr_off = entry_off = None
    for t, size, off in walk_header(data, hdr):
        if t == 2:
            addr_off = off
        elif t == 3:
            entry_off = off
    if addr_off is None or entry_off is None:
        print("ERROR: address/entry tag missing")
        return 1
    # address tag: +8 header_addr +12 load_addr +16 load_end +20 bss_end
    struct.pack_into("<I", data, addr_off + 8, LOAD_BASE + 8)     # header_addr
    struct.pack_into("<I", data, addr_off + 12, LOAD_BASE)        # load_addr
    struct.pack_into("<I", data, addr_off + 16, load_end)         # load_end
    struct.pack_into("<I", data, addr_off + 20, load_end)         # bss_end (no .bss)
    # entry tag: +8 entry_addr
    struct.pack_into("<I", data, entry_off + 8, LOAD_BASE + 0x68)
    # kernel_boot_params right after the end tag
    for t, size, off in walk_header(data, hdr):
        if t == 0:
            bp = (off + size + 7) & ~7
            bss_start = struct.unpack_from("<I", data, bp)[0]
            if OLD_BASE <= bss_start < 0x100000000:
                struct.pack_into("<I", data, bp, bss_start + delta)
            break
    print("rebase: 0x%x -> 0x%x (delta 0x%x), load_end 0x%x" % (OLD_BASE, LOAD_BASE, delta, load_end))

    open(out_path, "wb").write(data)
    print("OK: %s (%d bytes)" % (out_path, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
