# -*- coding: utf-8 -*-
"""Build a GPT disk image with an EFI System Partition (ESP, FAT32) containing
GRUB (BOOTX64.EFI) + grub.cfg + modules + the nefuOS multiboot2 kernel.

Deterministic, zero external dependencies (no mtools/parted): the GPT header
and the FAT32 filesystem are written directly.  Layout:

    LBA 0            protective MBR (type 0xEE)
    LBA 1            GPT header
    LBA 2 .. 33      GPT partition entries (ESP type C12A7328-...)
    LBA 2048 ..      ESP partition (64 MiB, FAT32, 512 B clusters)
    last 33 LBAs     backup GPT entries + header

FAT32 layout inside the ESP (512 B/sector, 1 sector/cluster):
    sector 0        boot sector (BPB), backup at sector 6
    sector 1        FSInfo, backup at sector 7
    sector 32 ..    FAT1 / FAT2 (1016 sectors each)
    sector 2064 ..  data area (root dir = cluster 2)

Usage:
    python make_esp.py <grubx64.efi> <grub.cfg> <kernel.bin> <modules_dir> <out.img>
"""
import os
import struct
import sys
import zlib

SECTOR = 512
IMG_SECTORS = 262144          # 128 MiB image
ESP_START = 2048              # 1 MiB aligned
ESP_SECTORS = 131072          # 64 MiB ESP
ESP_END = ESP_START + ESP_SECTORS - 1

RESERVED = 32                 # reserved sectors (BPB)
FAT_SECTORS = 1016            # sectors per FAT
DATA_START = RESERVED + 2 * FAT_SECTORS   # 2064
DATA_CLUSTERS = ESP_SECTORS - DATA_START  # 129008
ROOT_CLUSTER = 2

# On-disk GPT GUIDs are stored mixed-endian: first 3 fields little-endian,
# last field as-is.  EFI System Partition type GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B
# therefore appears as 28 27 2A C1 1F F8 D2 11 BA 4B 00 A0 C9 3E C9 3B.
ESP_GUID = bytes.fromhex("28272AC11FF8D211BA4B00A0C93EC93B")
DISK_GUID = bytes.fromhex("4E4546554F5300004E4546554F530000")
PART_GUID = bytes.fromhex("4E454650530000004E45465053000000")

TOTAL_FILE_BYTES = 0


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


# ---------------------------------------------------------------- GPT ----
def gpt_header(entries_blob, current_lba, backup_lba):
    hdr = bytearray(92)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<I", hdr, 8, 0x00010000)          # revision 1.0
    struct.pack_into("<I", hdr, 12, 92)                 # header size
    struct.pack_into("<I", hdr, 16, 0)                  # crc (computed below)
    struct.pack_into("<Q", hdr, 24, current_lba)
    struct.pack_into("<Q", hdr, 32, backup_lba)
    struct.pack_into("<Q", hdr, 40, 34)                 # first usable LBA
    struct.pack_into("<Q", hdr, 48, IMG_SECTORS - 34)   # last usable LBA
    hdr[56:72] = DISK_GUID
    struct.pack_into("<Q", hdr, 72, 2)                  # entries LBA
    struct.pack_into("<I", hdr, 80, 128)                # entry count
    struct.pack_into("<I", hdr, 84, 128)                # entry size
    struct.pack_into("<I", hdr, 88, crc32(entries_blob))
    struct.pack_into("<I", hdr, 16, crc32(hdr))
    return hdr


def gpt_entries():
    e = bytearray(128 * 128)
    e[0:16] = ESP_GUID
    e[16:32] = PART_GUID
    struct.pack_into("<Q", e, 32, ESP_START)
    struct.pack_into("<Q", e, 40, ESP_END)
    name = "EFI System Partition".encode("utf-16le")
    e[56:56 + len(name)] = name
    return e


def protective_mbr():
    m = bytearray(SECTOR)
    m[510:512] = b"\x55\xAA"
    m[450] = 0xEE                                    # GPT protective type
    struct.pack_into("<I", m, 454, 1)                # start LBA
    struct.pack_into("<I", m, 458, IMG_SECTORS - 1)  # size
    return m


# --------------------------------------------------------------- FAT32 ----
def fat_bpb(vol_serial, hidden):
    b = bytearray(SECTOR)
    b[0:3] = b"\xEB\x58\x90"
    b[3:11] = b"NEFUOS  "
    struct.pack_into("<H", b, 11, 512)               # bytes/sector
    b[13] = 1                                        # sectors/cluster
    struct.pack_into("<H", b, 14, RESERVED)
    b[16] = 2                                        # FATs
    struct.pack_into("<H", b, 17, 0)                 # root entries (FAT32 = 0)
    struct.pack_into("<H", b, 19, 0)                 # total sectors 16
    b[21] = 0xF8                                     # media
    struct.pack_into("<H", b, 22, 0)                 # FAT16 size
    struct.pack_into("<H", b, 24, 63)                # sectors/track
    struct.pack_into("<H", b, 26, 255)               # heads
    struct.pack_into("<I", b, 28, hidden)            # hidden sectors = ESP start
    struct.pack_into("<I", b, 32, ESP_SECTORS)       # total sectors 32
    struct.pack_into("<I", b, 36, FAT_SECTORS)       # FAT32 size
    struct.pack_into("<H", b, 40, 0)                 # ext flags
    struct.pack_into("<H", b, 42, 0)                 # fs version
    struct.pack_into("<I", b, 44, ROOT_CLUSTER)      # root cluster
    struct.pack_into("<H", b, 48, 1)                 # FSInfo sector
    struct.pack_into("<H", b, 50, 6)                 # backup boot sector
    b[64] = 0x80                                     # drive number
    b[66] = 0x29                                     # boot signature
    struct.pack_into("<I", b, 67, vol_serial)
    b[71:82] = b"NEFUOS  ESP"
    b[82:90] = b"FAT32   "
    b[510:512] = b"\x55\xAA"
    return b


def fat_fsinfo(free_clusters, next_free):
    f = bytearray(SECTOR)
    struct.pack_into("<I", f, 0, 0x41615252)
    struct.pack_into("<I", f, 484, 0x61417272)
    struct.pack_into("<I", f, 488, free_clusters)
    struct.pack_into("<I", f, 492, next_free)
    struct.pack_into("<I", f, 508, 0xAA550000)
    return f


def short_name_checksum(short11):
    s = 0
    for ch in short11.encode("ascii"):
        s = (((s & 1) << 7) | (s >> 1)) + ch
        s &= 0xFF
    return s


def make_short(long_name, used):
    # '.' / '..' pseudo-entries: fixed padded names, no LFN, no ~N
    if long_name in (".", ".."):
        return (long_name.upper() + " " * 8)[:8] + "   "
    parts = long_name.rsplit(".", 1)
    stem = parts[0]
    ext = parts[1] if len(parts) > 1 else ""
    stem = stem.upper()
    ext = ext.upper()[:3] if ext else ""
    base = "".join(c if (c.isalnum() or c in "_-^$~!#%&{}()@'`") else "_" for c in stem)
    if len(base) <= 8:
        sn = (base.ljust(8) + ext.ljust(3))[:11]
        if sn not in used:
            used.add(sn)
            return sn
    for i in range(1, 10000):
        tail = "~%d" % i
        head = (base[:8 - len(tail)] + tail)[:8]
        sn = (head + ext).ljust(11)[:11]
        if sn not in used:
            used.add(sn)
            return sn
    raise RuntimeError("no short name for %r" % long_name)


def lfn_entries(long_name, short11):
    units = []
    name = long_name.encode("utf-16le")
    for i in range(0, len(name), 26):
        units.append(name[i:i + 26])
    units = units[::-1]
    entries = []
    for idx, u in enumerate(units):
        e = bytearray(32)
        seq = len(units) - idx
        if idx == 0:
            seq |= 0x40
        e[0] = seq
        chunk = u + b"\x00" * (26 - len(u))
        e[1:11] = chunk[0:10]
        e[11] = 0x0F
        e[13] = short_name_checksum(short11)
        e[14:26] = chunk[10:22]
        e[28:32] = chunk[22:26]
        entries.append(e)
    return entries


class FatFS:
    def __init__(self, img, base_lba):
        self.img = img
        self.base = base_lba * SECTOR
        self.fat = [0x0FFFFFF8, 0x0FFFFFFF]
        self.next_cluster = ROOT_CLUSTER + 1   # cluster 2 is the root dir; files start at 3
        self.free = DATA_CLUSTERS - 1      # root cluster (2) already consumed
        self.used_short = set()

    def alloc(self, nclusters):
        start = self.next_cluster
        # grow the table to cover [start, start+nclusters-1] (index 2 = root
        # dir is already EOC); the chain is set afterwards
        while len(self.fat) < start + nclusters:
            self.fat.append(0x0FFFFFFF)
        for i in range(nclusters - 1):
            self.fat[start + i] = start + i + 1
        self.fat[start + nclusters - 1] = 0x0FFFFFFF
        self.next_cluster += nclusters
        self.free -= nclusters
        return start

    def cluster_offset(self, c):
        return self.base + DATA_START * SECTOR + (c - ROOT_CLUSTER) * SECTOR

    def write_file(self, data):
        n = (len(data) + SECTOR - 1) // SECTOR
        if n == 0:
            return 0
        start = self.alloc(n)
        off = self.cluster_offset(start)
        self.img[off:off + len(data)] = data
        return start

    def dir_entry(self, long_name, attr, start_cluster, size):
        short11 = make_short(long_name, self.used_short)
        entries = []
        if long_name not in (".", ".."):
            entries += lfn_entries(long_name, short11)
        e = bytearray(32)
        e[0:11] = short11.encode("ascii")
        e[11] = attr
        struct.pack_into("<H", e, 20, start_cluster >> 16)
        struct.pack_into("<H", e, 26, start_cluster & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        entries.append(e)
        return entries


class FatDir:
    def __init__(self, fs, name):
        self.fs = fs
        self.blob = bytearray()
        self.name = name
        self.start = None

    def add(self, long_name, attr, start_cluster, size):
        for e in self.fs.dir_entry(long_name, attr, start_cluster, size):
            self.blob += e

    def finalize(self, parent):
        """Write this directory's blob to fresh clusters and add its entry to
        the parent's blob (parent must NOT be finalized yet)."""
        n = (len(self.blob) + SECTOR - 1) // SECTOR
        if n == 0:
            n = 1
        self.blob += b"\x00" * (n * SECTOR - len(self.blob))
        self.start = self.fs.alloc(n)
        off = self.fs.cluster_offset(self.start)
        self.fs.img[off:off + len(self.blob)] = self.blob
        if parent is not None:
            parent.add(self.name, 0x10, self.start, 0)
        return self.start

    def patch_dotdot(self, parent_cluster):
        """Fix '.' and '..' cluster links (stored as 0 while building)."""
        off = self.fs.cluster_offset(self.start)
        self.fs.img[off + 20:off + 22] = struct.pack("<H", self.start >> 16)
        self.fs.img[off + 26:off + 28] = struct.pack("<H", self.start & 0xFFFF)
        self.fs.img[off + 52:off + 54] = struct.pack("<H", parent_cluster >> 16)
        self.fs.img[off + 58:off + 60] = struct.pack("<H", parent_cluster & 0xFFFF)


def build(files, out_path):
    global TOTAL_FILE_BYTES
    img = bytearray(IMG_SECTORS * SECTOR)

    # ---- protective MBR + GPT ----
    entries = gpt_entries()
    img[0:SECTOR] = protective_mbr()
    img[1 * SECTOR:2 * SECTOR] = gpt_header(entries, 1, IMG_SECTORS - 1).ljust(SECTOR, b"\x00")
    img[2 * SECTOR:34 * SECTOR] = entries
    # explicit fixed-size slots: a slice assignment whose RHS is shorter than
    # the slice silently SHRINKS the bytearray and shifts the backup GPT
    b_entries_off = (IMG_SECTORS - 33) * SECTOR
    img[b_entries_off:b_entries_off + 32 * SECTOR] = entries
    b_hdr_off = (IMG_SECTORS - 1) * SECTOR
    img[b_hdr_off:b_hdr_off + SECTOR] = gpt_header(entries, IMG_SECTORS - 1, 1).ljust(SECTOR, b"\x00")
    assert len(img) == IMG_SECTORS * SECTOR, "image length %d != %d" % (len(img), IMG_SECTORS * SECTOR)

    # ---- FAT32 ----
    fs = FatFS(img, ESP_START)
    bpb = fat_bpb(0x4E454650, ESP_START)
    img[fs.base:fs.base + SECTOR] = bpb
    img[fs.base + 6 * SECTOR:fs.base + 7 * SECTOR] = bpb
    fat_off1 = fs.base + RESERVED * SECTOR
    fat_off2 = fat_off1 + FAT_SECTORS * SECTOR

    # ---- directory tree (children finalized before parents) ----
    root = FatDir(fs, "/")
    efi = FatDir(fs, "EFI")
    boot = FatDir(fs, "BOOT")
    debian = FatDir(fs, "debian")
    nefuos = FatDir(fs, "nefuos")
    moddir = FatDir(fs, "x86_64-efi")
    bootmoddir = FatDir(fs, "x86_64-efi")

    root.add(".", 0x10, 0, 0)
    root.add("..", 0x10, 0, 0)
    efi.add(".", 0x10, 0, 0)
    efi.add("..", 0x10, 0, 0)
    boot.add(".", 0x10, 0, 0)
    boot.add("..", 0x10, 0, 0)
    debian.add(".", 0x10, 0, 0)
    debian.add("..", 0x10, 0, 0)
    nefuos.add(".", 0x10, 0, 0)
    nefuos.add("..", 0x10, 0, 0)
    moddir.add(".", 0x10, 0, 0)
    moddir.add("..", 0x10, 0, 0)
    bootmoddir.add(".", 0x10, 0, 0)
    bootmoddir.add("..", 0x10, 0, 0)

    # ---- allocate file data ----
    boot_files = {}
    debian_files = {}
    nefuos_files = {}
    mod_files = {}
    mod_boot_files = {}
    for name, data in sorted(files.items()):
        start = fs.write_file(data)
        TOTAL_FILE_BYTES += len(data)
        if name == "EFI/BOOT/BOOTX64.EFI":
            boot_files["BOOTX64.EFI"] = (start, len(data))
        elif name == "EFI/BOOT/grub.cfg":
            boot_files["grub.cfg"] = (start, len(data))
        elif name == "EFI/debian/grub.cfg":
            debian_files["grub.cfg"] = (start, len(data))
        elif name.startswith("modules/"):
            mod_files[name[len("modules/"):]] = (start, len(data))
        elif name.startswith("modules_boot/"):
            mod_boot_files[name[len("modules_boot/"):]] = (start, len(data))
        elif name == "EFI/nefuos/nefuos.bin":
            nefuos_files["nefuos.bin"] = (start, len(data))
        else:
            raise RuntimeError("unhandled file " + name)

    # ---- directory entries ----
    for n, (c, sz) in sorted(boot_files.items()):
        boot.add(n, 0x20, c, sz)
    for n, (c, sz) in sorted(debian_files.items()):
        debian.add(n, 0x20, c, sz)
    for n, (c, sz) in sorted(nefuos_files.items()):
        nefuos.add(n, 0x20, c, sz)
    for n, (c, sz) in sorted(mod_files.items()):
        moddir.add(n, 0x20, c, sz)
    for n, (c, sz) in sorted(mod_boot_files.items()):
        bootmoddir.add(n, 0x20, c, sz)

    # ---- finalize bottom-up ----
    moddir.finalize(debian)      # /EFI/debian/x86_64-efi -> debian
    bootmoddir.finalize(boot)    # /EFI/BOOT/x86_64-efi -> boot
    boot.finalize(efi)           # /EFI/BOOT -> efi
    debian.finalize(efi)         # /EFI/debian -> efi
    nefuos.finalize(efi)         # /EFI/nefuos -> efi
    efi.finalize(root)           # /EFI -> root

    # root dir content (cluster 2)
    root_blob = bytes(root.blob).ljust(SECTOR, b"\x00")
    off = fs.cluster_offset(ROOT_CLUSTER)
    img[off:off + SECTOR] = root_blob

    # fix '.' / '..' cluster links
    moddir.patch_dotdot(debian.start)
    bootmoddir.patch_dotdot(boot.start)
    boot.patch_dotdot(efi.start)
    debian.patch_dotdot(efi.start)
    nefuos.patch_dotdot(efi.start)
    efi.patch_dotdot(ROOT_CLUSTER)
    off = fs.cluster_offset(ROOT_CLUSTER)
    img[off + 20:off + 22] = struct.pack("<H", ROOT_CLUSTER >> 16)
    img[off + 26:off + 28] = struct.pack("<H", ROOT_CLUSTER & 0xFFFF)
    img[off + 52:off + 54] = struct.pack("<H", ROOT_CLUSTER >> 16)
    img[off + 58:off + 60] = struct.pack("<H", ROOT_CLUSTER & 0xFFFF)

    # ---- FAT tables ----
    fat_bytes = b"".join(struct.pack("<I", v) for v in fs.fat)
    fat_bytes = fat_bytes.ljust(FAT_SECTORS * SECTOR, b"\x00")
    img[fat_off1:fat_off1 + len(fat_bytes)] = fat_bytes
    img[fat_off2:fat_off2 + len(fat_bytes)] = fat_bytes

    # ---- FSInfo ----
    fsinfo = fat_fsinfo(fs.free, fs.next_cluster)
    img[fs.base + 1 * SECTOR:fs.base + 2 * SECTOR] = fsinfo
    img[fs.base + 7 * SECTOR:fs.base + 8 * SECTOR] = fsinfo

    with open(out_path, "wb") as f:
        f.write(img)
    print("ESP image OK: %s (%d bytes, %.1f MiB)" % (out_path, len(img), len(img) / 1048576))
    print("  ESP: LBA %d..%d (64 MiB, FAT32, %d clusters, %d free)" % (ESP_START, ESP_END, DATA_CLUSTERS, fs.free))
    print("  payload: %d files, %d bytes" % (len(files), TOTAL_FILE_BYTES))
    return 0


def main():
    if len(sys.argv) != 6:
        print("usage: make_esp.py <grubx64.efi> <grub.cfg> <kernel.bin> <modules_dir> <out.img>")
        return 2
    efi_path, cfg_path, kernel_path, mods_dir, out_path = sys.argv[1:6]
    files = {}
    files["EFI/BOOT/BOOTX64.EFI"] = open(efi_path, "rb").read()
    cfg = open(cfg_path, "rb").read()
    files["EFI/BOOT/grub.cfg"] = cfg
    files["EFI/debian/grub.cfg"] = cfg
    files["EFI/nefuos/nefuos.bin"] = open(kernel_path, "rb").read()
    mod_count = 0
    for root, dirs, names in os.walk(mods_dir):
        for n in names:
            if n.endswith(".mod"):
                files["modules/" + n] = open(os.path.join(root, n), "rb").read()
                files["modules_boot/" + n] = files["modules/" + n]
                mod_count += 1
    print("modules staged: %d (x2: /EFI/debian + /EFI/BOOT fallback)" % mod_count)
    return build(files, out_path)


if __name__ == "__main__":
    sys.exit(main())
