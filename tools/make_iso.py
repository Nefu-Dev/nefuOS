# -*- coding: utf-8 -*-
"""Build a minimal ISO9660 + El Torito (no-emulation) bootable ISO.
Replaces xorriso which is unreliable in this MSYS2-on-Windows environment.
Layout:
  LBA 16: PVD, 17: Boot Record VD, 18: Terminator VD, 19: Boot Catalog,
  20: PathTable L, 21: PathTable M, 22: root dir data,
  23: boot.s (2048B-aligned, padded), 24+: kernel.bin (padded to 64 x 2048B).

Boot chain (no-emulation):
  SeaBIOS reads 1 sector (2048B) at LBA 23 = boot.s into 0x7C00,
  then boot.s itself reads kernel.bin via int13 AH=0x42 from LBA 24 in two
  32-sector (64KB) chunks: LBA 24..55 -> 0x20000, LBA 56..87 -> 0x30000.
  (Only 32-sector chunks work reliably on QEMU's ATAPI; kernel is padded to
  64 blocks in the ISO so the second chunk never runs off the end.)
"""
import struct
import sys

BS = 2048
LBA_PVD = 16
LBA_BR = 17
LBA_TERM = 18
LBA_BOOTCAT = 19
LBA_PTL = 20
LBA_PTM = 21
LBA_ROOT = 22
LBA_IMG = 23          # boot.s (no-emulation boot image, 1 sector)
LBA_KERNEL = 24       # kernel.bin
KERNEL_BLOCKS = 128    # padded (57 used + 7 zeros) so boot's 2nd 32-sector chunk is safe

VOL = b"NEFUOS"


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def d7():
    return bytes(7)  # unknown date/time, valid


def dir_rec(lba, length, flags, name):
    """ISO9660 directory record."""
    n = len(name)
    rec = bytes([33 + n, 0]) + both32(lba) + both32(length) + d7() + bytes([flags, 0, 0]) + both16(1) + bytes([n]) + name
    if len(rec) % 2:
        rec += b"\x00"
    return rec


def build(boot_path, kernel_path, iso_path):
    with open(boot_path, "rb") as f:
        boot = f.read()
    assert len(boot) == 512, "boot.bin must be 512B"
    with open(kernel_path, "rb") as f:
        kernel = f.read()
    assert len(kernel) <= KERNEL_BLOCKS * BS, "kernel too large for 64 blocks"
    vol_size = LBA_KERNEL + KERNEL_BLOCKS

    # ---- PVD ----
    pvd = bytearray(BS)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = VOL.ljust(32, b" ")
    pvd[40:72] = VOL.ljust(32, b" ")
    pvd[80:88] = both32(vol_size)
    pvd[120:124] = both16(1)          # volume set size
    pvd[124:128] = both16(1)          # volume sequence number
    pvd[128:132] = both16(BS)         # logical block size
    pvd[132:140] = both32(8)          # path table size (one root record)
    pvd[140:144] = struct.pack("<I", LBA_PTL)
    pvd[144:148] = struct.pack("<I", 0)
    pvd[148:152] = struct.pack(">I", LBA_PTM)
    pvd[152:156] = struct.pack(">I", 0)
    pvd[156:190] = dir_rec(LBA_ROOT, BS, 2, b"\x00")  # root record, 34 bytes
    pvd[190:318] = VOL.ljust(128, b" ")
    pvd[318:446] = b"NEfuOS 0.1".ljust(128, b" ")
    pvd[446:574] = b"NEfuOS".ljust(128, b" ")
    pvd[574:702] = b"NEfuOS bare kernel".ljust(128, b" ")
    pvd[813:829] = b"2026090612000000" + b"\x00"

    # ---- Boot Record VD (El Torito) ----
    br = bytearray(BS)
    br[0] = 0
    br[1:6] = b"CD001"
    br[6] = 1
    br[7:39] = b"EL TORITO SPECIFICATION" + b"\x00" * 9  # must end with NUL (SeaBIOS strcmp)
    br[71:75] = struct.pack("<I", LBA_BOOTCAT)

    # ---- Terminator VD ----
    tm = bytearray(BS)
    tm[0] = 0xFF
    tm[1:6] = b"CD001"
    tm[6] = 1

    # ---- Boot Catalog ----
    bc = bytearray(BS)
    bc[0] = 1                      # validation entry
    bc[1] = 0                      # platform: x86
    bc[2:4] = b"\x00\x00"
    bc[4:28] = b"NEfuOS".ljust(24, b" ")
    bc[30:32] = b"\x55\xAA"          # signature must be inside checksum
    # checksum: sum of first 16 big-endian words (incl. signature) == 0 mod 65536
    s = 0
    for i in range(0, 32, 2):
        s += (bc[i] << 8) | bc[i + 1]
    bc[28:30] = struct.pack(">H", (0x10000 - (s & 0xFFFF)) & 0xFFFF)
    bc[32] = 0x88                  # initial/default entry
    bc[33] = 0x00                  # media: no emulation
    bc[34:36] = b"\x00\x00"        # load segment 0 (default 0x7C0)
    bc[36] = 0                     # system type
    bc[37] = 0
    bc[38:40] = struct.pack("<H", 1)      # sector count: 1 (only boot.s, 2048B)
    bc[40:44] = struct.pack("<I", LBA_IMG)  # boot image LBA (boot.s)

    # ---- Path Tables ----
    ptl = bytearray(8)             # root only
    ptl[0] = 1
    ptl[1] = 0
    ptl[2:6] = struct.pack("<I", LBA_ROOT)
    ptl[6] = 0
    ptm = bytearray(8)
    ptm[0] = 1
    ptm[1] = 0
    ptm[2:6] = struct.pack(">I", LBA_ROOT)
    ptm[6] = 0

    # ---- Root directory data (NEFUOS.BIN boot + NEFUOS.KRN kernel) ----
    root = bytearray(BS)
    dot = dir_rec(LBA_ROOT, BS, 2, b"\x00")
    root[0:len(dot)] = dot                                        # "."
    root[len(dot):len(dot) * 2] = dot                             # ".."
    frec = dir_rec(LBA_IMG, 2048, 0, b"NEFUOS.BIN;1")
    root[len(dot) * 2:len(dot) * 2 + len(frec)] = frec
    krec = dir_rec(LBA_KERNEL, len(kernel), 0, b"NEFUOS.KRN;1")
    root[len(dot) * 2 + len(frec):len(dot) * 2 + len(frec) + len(krec)] = krec

    # ---- Assemble ----
    out = bytearray(vol_size * BS)
    out[LBA_PVD * BS:(LBA_PVD + 1) * BS] = pvd
    out[LBA_BR * BS:(LBA_BR + 1) * BS] = br
    out[LBA_TERM * BS:(LBA_TERM + 1) * BS] = tm
    out[LBA_BOOTCAT * BS:(LBA_BOOTCAT + 1) * BS] = bc
    out[LBA_PTL * BS:(LBA_PTL + 1) * BS] = ptl
    out[LBA_PTM * BS:(LBA_PTM + 1) * BS] = ptm
    out[LBA_ROOT * BS:(LBA_ROOT + 1) * BS] = root
    out[LBA_IMG * BS:(LBA_IMG + 1) * BS] = boot + bytes(BS - len(boot))
    out[LBA_KERNEL * BS:LBA_KERNEL * BS + len(kernel)] = kernel

    with open(iso_path, "wb") as f:
        f.write(out)
    print("ISO OK: %d bytes, %d blocks (kernel %d blocks)" % (len(out), vol_size, KERNEL_BLOCKS))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: python make_iso.py <boot.bin> <kernel.bin> <out.iso>")
        sys.exit(1)
    build(sys.argv[1], sys.argv[2], sys.argv[3])
