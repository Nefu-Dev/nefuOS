# -*- coding: utf-8 -*-
"""Rebin-Kernel replacement: parse PE section table, assemble flat kernel.bin.

Same logic as the old PowerShell Rebin-Kernel function (RVA-ordered sections,
skip .reloc / zero-raw sections), but deterministic and self-verifying:

  * prints REBIN_OK <max_end> <bss_start> <bss_size> on success so
    build_iso.ps1 can parse bss values for patch_mb2;
  * asserts the multiboot2 magic (0xE85250D6) is at flat offset 8 and that the
    first bytes look like the entry code (jmp rel8 at offset 0), so a stale or
    misassembled kernel fails LOUDLY here instead of dying in patch_mb2.

Usage: python rebin_kernel.py <kernel.exe> <kernel.bin>
"""
import struct
import sys

LOAD_BASE = 0x100000
MB2_MAGIC = 0xE85250D6


def main():
    if len(sys.argv) != 3:
        print("usage: rebin_kernel.py <kernel.exe> <kernel.bin>")
        return 2
    in_exe, out_bin = sys.argv[1], sys.argv[2]
    with open(in_exe, "rb") as f:
        exe = f.read()

    if exe[:2] != b"MZ":
        print("ERROR: not a PE file (no MZ header)")
        return 1
    pe_off = struct.unpack_from("<I", exe, 0x3C)[0]
    if exe[pe_off:pe_off + 4] != b"PE\0\0":
        print("ERROR: bad PE signature")
        return 1
    num_sec = struct.unpack_from("<H", exe, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", exe, pe_off + 20)[0]
    sec_tab = pe_off + 24 + opt_size

    secs = []
    for i in range(num_sec):
        off = sec_tab + i * 40
        name = exe[off:off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        vs, va, raw_size, raw_ptr = struct.unpack_from("<IIII", exe, off + 8)
        secs.append((name, va, vs, raw_size, raw_ptr))

    bss_rva, bss_vsize = 0, 0
    max_end = 0
    for name, rva, vs, raw_size, raw_ptr in secs:
        if name == ".bss":
            bss_rva, bss_vsize = rva, vs
        if name == ".reloc" or raw_size == 0:
            continue
        max_end = max(max_end, rva + raw_size)

    if bss_vsize <= 0 and bss_rva > 0:
        nxt = min((s[1] for s in secs if s[1] > bss_rva), default=max_end)
        bss_vsize = nxt - bss_rva

    out = bytearray(max_end)
    for name, rva, vs, raw_size, raw_ptr in secs:
        if name == ".reloc" or raw_size == 0 or raw_ptr == 0:
            continue
        out[rva:rva + raw_size] = exe[raw_ptr:raw_ptr + raw_size]

    # self-verify: multiboot2 header must sit at flat offset 8 (entry.s layout)
    if len(out) < 16:
        print("ERROR: kernel.bin too small")
        return 1
    magic = struct.unpack_from("<I", out, 8)[0]
    if magic != MB2_MAGIC:
        print("ERROR: multiboot2 magic missing at offset 8 (got 0x%08X) -- stale or misassembled kernel?" % magic)
        return 1
    # offset 0 should be a short jump (EB xx) over the header
    if out[0] != 0xEB:
        print("ERROR: expected jmp (EB) at offset 0, got 0x%02X" % out[0])
        return 1

    with open(out_bin, "wb") as f:
        f.write(out)

    bss_start = bss_rva + LOAD_BASE if bss_rva > 0 else 0
    print("REBIN_OK %d 0x%X 0x%X" % (max_end, bss_start, bss_vsize))
    print("sections=%d max_end=0x%X bss_start=0x%X bss_size=0x%X (%d bytes)" %
          (num_sec, max_end, bss_start, bss_vsize, len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
