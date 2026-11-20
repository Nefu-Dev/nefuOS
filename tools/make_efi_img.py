# -*- coding: utf-8 -*-
"""Build a bare FAT16 El Torito EFI boot image for nefuOS.

This produces the "EFI System Partition image" that an El Torito *EFI* boot
record points at.  Unlike make_esp.py (which writes a full GPT disk image for
the ESP+GRUB test path), the payload here is a standalone FAT filesystem image
with NO partition table — exactly what UEFI firmware mounts when it boots the
EFI El Torito entry of an ISO.  This is how every Linux live ISO's efiboot.img
works, and it is what makes the ISO bootable on UEFI-only machines (e.g. a
Huawei MateBook) instead of only under SeaBIOS.

The image is self-contained: it carries GRUB (BOOTX64.EFI), the GRUB modules
needed for multiboot2 booting, grub.cfg, AND the nefuOS kernel, so GRUB loads
the kernel from the same FAT volume it booted from.  That works both when the
firmware launches the ISO's EFI record and when the image is written to a USB
stick on its own.

Why FAT16 and not FAT32
-----------------------
The El Torito boot catalog stores the image size as a 16-bit count of 512 B
sectors, so the image is hard-capped at 32 MiB.  A FAT32 volume is only
spec-legal with >= 65525 clusters, which at 512 B/cluster would need a ~32 MiB
volume — leaving no room for the payload.  Small FAT32 images are also actively
dangerous: firmware FAT drivers classify a volume by its *cluster count*, not by
the BPB's filesystem-type string, so a 4000-cluster "FAT32" gets parsed as
FAT16 (2-byte entries, fixed root directory) and the mount produces garbage.
FAT16 with 2 KiB clusters lands at ~7600 clusters, squarely inside the
4085..65524 FAT16 range, and is the geometry real firmware expects for an image
this size.

Why a module *closure* and not the whole module directory
---------------------------------------------------------
tools/grub_toolchain ships 293 .mod files (14 MiB).  Staging them twice (once
per GRUB prefix) plus the 9.8 MiB kernel would blow past the 32 MiB El Torito
ceiling.  So this script reads GRUB's own moddep.lst and stages only the
transitive dependency closure of the modules grub_iso.cfg insmods (~45 files,
0.7 MiB per prefix).  Over-staging stays safe: GRUB returns the already-loaded
handle for a module compiled into the monolith, so an unused .mod costs bytes
but never breaks the boot.

Why the modules are staged under two prefixes
---------------------------------------------
The monolithic BOOTX64.EFI in tools/grub_toolchain was built with
prefix=/EFI/debian (that is the only "/EFI/..." string in the binary), so GRUB
reads /EFI/debian/grub.cfg and /EFI/debian/x86_64-efi/*.mod.  When firmware
loads an El Torito EFI image, though, some implementations report the boot
device such that GRUB resolves its prefix to /EFI/BOOT instead.  Both trees are
therefore populated so the config and modules are found either way.

Layout (FAT16, 512 B/sector, 4 sectors/cluster = 2 KiB, base LBA 0):
    sector 0            boot sector (BPB)
    sector 1 .. 2F      FAT1 / FAT2
    then                root directory (512 fixed entries)
    then                data area (clusters)

    /EFI/BOOT/BOOTX64.EFI          <- GRUB
    /EFI/BOOT/grub.cfg             <- config
    /EFI/BOOT/x86_64-efi/*.mod     <- modules
    /EFI/debian/grub.cfg           <- config (compiled-in prefix)
    /EFI/debian/x86_64-efi/*.mod   <- modules (compiled-in prefix)
    /EFI/nefuos/nefuos.bin         <- the multiboot2 kernel

Usage:
    python make_efi_img.py <grubx64.efi> <grub.cfg> <kernel.bin> <modules_dir> <out.img>

<modules_dir> is the GRUB x86_64-efi module directory; its sibling moddep.lst
supplies the dependency graph.  The build self-verifies by re-reading the
finished image and comparing every staged file byte-for-byte.
"""
import os
import struct
import sys

SECTOR = 512
RESERVED = 1                  # boot sector only (FAT16 has no FSInfo/backup)
ROOT_ENTRIES = 512            # fixed root directory size
ROOT_DIR_SECTORS = ROOT_ENTRIES * 32 // SECTOR
ROOT_CLUSTER = 0              # FAT16 root directory is not part of the cluster heap
FAT16_EOC = 0xFFFF
FAT16_MIN_CLUSTERS = 4085     # below this a volume is FAT12
FAT16_MAX_CLUSTERS = 65524    # at/above this it must be FAT32
EL_TORITO_MAX_SECTORS = 0xFFFF  # 16-bit sector count in the boot catalog
# Modules grub_iso.cfg asks for; their dependencies arrive via the moddep closure.
CFG_ROOTS = ["multiboot2", "all_video", "video", "efi_gop", "gfxterm", "search",
             "search_fs_file", "echo", "normal", "configfile", "ls", "test",
             "reboot", "halt", "part_msdos", "part_gpt", "fat", "iso9660",
             "chain", "linux", "boot", "mmap", "relocator", "elf", "acpi", "net"]


class BuildError(Exception):
    pass


# --------------------------------------------------------------- helpers ----
def short_name_checksum(short11):
    s = 0
    for ch in short11.encode("ascii"):
        s = (((s & 1) << 7) | (s >> 1)) + ch
        s &= 0xFF
    return s


def make_short(long_name, used):
    """Generate a unique 8.3 short name.  LFN-carrying entries still need one,
    and firmware that ignores LFN entries falls back to exactly this field."""
    if long_name in (".", ".."):
        return (long_name.upper() + " " * 8)[:8] + "   "
    parts = long_name.rsplit(".", 1)
    stem = parts[0].upper()
    ext = parts[1].upper()[:3] if len(parts) > 1 else ""
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
    raise BuildError("no short name available for %r" % long_name)


def lfn_entries(long_name, short11):
    """Long-file-name directory entries, highest sequence number first."""
    name = long_name.encode("utf-16le")
    units = [name[i:i + 26] for i in range(0, len(name), 26)][::-1]
    entries = []
    cksum = short_name_checksum(short11)
    for idx, u in enumerate(units):
        e = bytearray(32)
        seq = len(units) - idx
        if idx == 0:
            seq |= 0x40
        e[0] = seq
        chunk = u + b"\x00" * (26 - len(u))
        e[1:11] = chunk[0:10]
        e[11] = 0x0F                              # ATTR_LONG_NAME
        e[13] = cksum
        e[14:26] = chunk[10:22]
        e[28:32] = chunk[22:26]
        entries.append(e)
    return entries


def read_moddep(mods_dir):
    lst = os.path.join(mods_dir, "moddep.lst")
    dep = {}
    if not os.path.exists(lst):
        return dep
    with open(lst, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or ":" not in line:
                continue
            name, rest = line.split(":", 1)
            dep[name.strip()] = set(x.strip() for x in rest.split() if x.strip())
    return dep


def module_closure(mods_dir):
    """Transitive dependency closure of CFG_ROOTS, restricted to modules that
    actually exist as <name>.mod (the rest are compiled into the monolith)."""
    dep = read_moddep(mods_dir)
    need, stack = set(), list(CFG_ROOTS)
    while stack:
        m = stack.pop()
        if m in need:
            continue
        need.add(m)
        for d in dep.get(m, ()):
            stack.append(d)
    return sorted(m for m in need if os.path.exists(os.path.join(mods_dir, m + ".mod")))


# ---------------------------------------------------------------- geometry --
def fat_sectors_for(data_clusters):
    """Sectors needed for one FAT16 table covering data_clusters + 2 reserved."""
    return max(((data_clusters + 2) * 2 + SECTOR - 1) // SECTOR, 1)


def pick_geometry(total_file_bytes, dir_count):
    """Pick (spc, fat_sectors, data_clusters, volume_sectors) so the volume is
    legal FAT16 and fits the El Torito 16-bit sector count.

    Larger clusters are preferred (fewer FAT sectors, faster firmware reads);
    they are rejected when they push the cluster count below the FAT16 minimum.
    """
    for spc in (8, 4, 2, 1):
        cluster = SECTOR * spc
        file_clusters = (total_file_bytes + cluster - 1) // cluster
        # every directory costs >= 1 cluster; add slack for LFN growth
        data_clusters = file_clusters + dir_count + 32
        fat_sectors = fat_sectors_for(data_clusters)
        volume_sectors = 0
        for _ in range(32):
            volume_sectors = RESERVED + 2 * fat_sectors + ROOT_DIR_SECTORS + data_clusters * spc
            mb = (volume_sectors * SECTOR + (1 << 20) - 1) >> 20
            volume_sectors = mb * (1 << 20) // SECTOR
            new_data = (volume_sectors - RESERVED - 2 * fat_sectors - ROOT_DIR_SECTORS) // spc
            new_fat = fat_sectors_for(new_data)
            if new_data == data_clusters and new_fat == fat_sectors:
                break
            data_clusters, fat_sectors = new_data, new_fat
        if not (FAT16_MIN_CLUSTERS <= data_clusters <= FAT16_MAX_CLUSTERS):
            continue                                  # try a smaller cluster
        if volume_sectors > EL_TORITO_MAX_SECTORS:
            continue                                  # exceeds the El Torito cap
        return spc, fat_sectors, data_clusters, volume_sectors
    raise BuildError("no FAT16-compatible geometry fits %d bytes under the 32 MiB "
                     "El Torito limit" % total_file_bytes)


def fat_bpb(vol_sectors, fat_sectors, spc):
    b = bytearray(SECTOR)
    b[0:3] = b"\xEB\x3C\x90"                          # jmp over the BPB
    b[3:11] = b"NEFUOS  "
    struct.pack_into("<H", b, 11, SECTOR)             # bytes/sector
    b[13] = spc                                       # sectors/cluster
    struct.pack_into("<H", b, 14, RESERVED)           # reserved sectors
    b[16] = 2                                         # number of FATs
    struct.pack_into("<H", b, 17, ROOT_ENTRIES)       # root directory entries
    if vol_sectors <= 0xFFFF:
        struct.pack_into("<H", b, 19, vol_sectors)    # total sectors 16
        struct.pack_into("<I", b, 32, 0)
    else:
        struct.pack_into("<H", b, 19, 0)
        struct.pack_into("<I", b, 32, vol_sectors)    # total sectors 32
    b[21] = 0xF8                                      # media descriptor
    struct.pack_into("<H", b, 22, fat_sectors)        # sectors per FAT
    struct.pack_into("<H", b, 24, 63)                 # sectors/track
    struct.pack_into("<H", b, 26, 255)                # heads
    struct.pack_into("<I", b, 28, 0)                  # hidden sectors (bare image)
    b[36] = 0x80                                      # drive number
    b[37] = 0                                         # reserved
    b[38] = 0x29                                      # extended boot signature
    struct.pack_into("<I", b, 39, 0x4E454645)         # volume serial
    b[43:54] = b"NEFUOS  EFI "                        # volume label (11 bytes)
    b[54:62] = b"FAT16   "                            # filesystem type
    b[510:512] = b"\x55\xAA"
    return b


# -------------------------------------------------------------------- FAT ----
class FatFS:
    """Minimal FAT16 allocator over a bytearray image."""

    def __init__(self, img, spc, fat_sectors, data_clusters):
        self.img = img
        self.spc = spc
        self.cluster = SECTOR * spc
        self.fat_sectors = fat_sectors
        self.data_clusters = data_clusters
        self.root_dir_off = (RESERVED + 2 * fat_sectors) * SECTOR
        self.data_off = self.root_dir_off + ROOT_DIR_SECTORS * SECTOR
        self.fat = [0xFFF8, FAT16_EOC]                # clusters 0,1 reserved
        self.next_cluster = 2
        self.free = data_clusters
        self.used_short = set()

    def alloc(self, nclusters):
        if nclusters > self.free:
            raise BuildError("volume overflow: need %d clusters, %d free"
                             % (nclusters, self.free))
        start = self.next_cluster
        while len(self.fat) < start + nclusters:
            self.fat.append(FAT16_EOC)
        for i in range(nclusters - 1):
            self.fat[start + i] = start + i + 1
        self.fat[start + nclusters - 1] = FAT16_EOC
        self.next_cluster += nclusters
        self.free -= nclusters
        return start

    def cluster_offset(self, c):
        return self.data_off + (c - 2) * self.cluster

    def write_file(self, data):
        n = (len(data) + self.cluster - 1) // self.cluster
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
        struct.pack_into("<H", e, 26, start_cluster & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        entries.append(e)
        return entries


class FatDir:
    """Accumulates directory entries, then writes them out (root is special:
    it occupies a fixed region instead of allocated clusters)."""

    def __init__(self, fs, name, is_root=False):
        self.fs = fs
        self.blob = bytearray()
        self.name = name
        self.is_root = is_root
        self.start = ROOT_CLUSTER if is_root else None

    def add(self, long_name, attr, start_cluster, size):
        for e in self.fs.dir_entry(long_name, attr, start_cluster, size):
            self.blob += e

    def finalize(self, parent):
        if self.is_root:
            limit = ROOT_DIR_SECTORS * SECTOR
            if len(self.blob) > limit:
                raise BuildError("root directory needs %d bytes, only %d available"
                                 % (len(self.blob), limit))
            self.blob += b"\x00" * (limit - len(self.blob))
            off = self.fs.root_dir_off
            self.fs.img[off:off + limit] = self.blob
            return ROOT_CLUSTER
        n = (len(self.blob) + self.fs.cluster - 1) // self.fs.cluster or 1
        self.blob += b"\x00" * (n * self.fs.cluster - len(self.blob))
        self.start = self.fs.alloc(n)
        off = self.fs.cluster_offset(self.start)
        self.fs.img[off:off + len(self.blob)] = self.blob
        if parent is not None:
            parent.add(self.name, 0x10, self.start, 0)
        return self.start

    def patch_dotdot(self, parent_cluster):
        """Fix the '.' / '..' cluster links (written as 0 while building)."""
        off = self.fs.cluster_offset(self.start)
        self.fs.img[off + 20:off + 22] = struct.pack("<H", self.start & 0xFFFF)
        self.fs.img[off + 26:off + 28] = struct.pack("<H", self.start & 0xFFFF)
        self.fs.img[off + 52:off + 54] = struct.pack("<H", parent_cluster & 0xFFFF)
        self.fs.img[off + 58:off + 60] = struct.pack("<H", parent_cluster & 0xFFFF)


# --------------------------------------------------------------- assembly ----
# ------------------------------------------------------------ verification --
def verify_image(img, expected):
    """Re-read the finished FAT16 image from scratch and compare every staged
    file byte-for-byte.  A silent layout bug here would show up as a machine
    that refuses to boot with no diagnostics, so the image is validated by an
    independent parser rather than trusting the writer.
    """
    if img[510:512] != b"\x55\xAA":
        raise BuildError("verify: missing boot signature")
    if img[54:62] != b"FAT16   ":
        raise BuildError("verify: filesystem type is %r, expected FAT16" % img[54:62])

    bps = struct.unpack_from("<H", img, 11)[0]
    spc = img[13]
    reserved = struct.unpack_from("<H", img, 14)[0]
    nfats = img[16]
    root_entries = struct.unpack_from("<H", img, 17)[0]
    fat_sectors = struct.unpack_from("<H", img, 22)[0]
    if bps != SECTOR:
        raise BuildError("verify: bytes/sector %d != %d" % (bps, SECTOR))

    fat_off = reserved * bps
    root_off = fat_off + nfats * fat_sectors * bps
    data_off = root_off + (root_entries * 32 // bps) * bps
    cluster = spc * bps

    def fat_entry(c):
        return struct.unpack_from("<H", img, fat_off + c * 2)[0]

    def read_chain(start, size):
        out = bytearray()
        c = start
        guard = 0
        while c >= 2 and c < 0xFFF0 and guard < 1 << 20:
            guard += 1
            off = data_off + (c - 2) * cluster
            out += img[off:off + cluster]
            c = fat_entry(c)
        return bytes(out[:size])

    def _short_to_name(short11):
        """Rebuild a display name from an 8.3 short entry (used when a file has
        no LFN entries, e.g. pure 8.3 names like BOOTX64.EFI)."""
        s = short11.rstrip()
        stem, ext = s[:8].rstrip(), s[8:].rstrip()
        return stem + ("." + ext if ext else "")

    def walk(off, limit_entries, prefix):
        """Yield (path, attr, cluster, size) for every entry in this directory."""
        found = []
        lfn_parts = []
        for i in range(limit_entries):
            e = off + i * 32
            first = img[e]
            if first == 0x00:
                break
            if first == 0xE5:
                lfn_parts = []
                continue
            attr = img[e + 11]
            if attr == 0x0F:                        # long file name entry
                seq = img[e] & 0x3F
                chunk = bytes(img[e + 1:e + 11] + img[e + 14:e + 26] + img[e + 28:e + 32])
                unit = chunk.decode("utf-16le", "replace").rstrip("\x00\uffff")
                if seq & 0x40:
                    lfn_parts = [unit]
                else:
                    lfn_parts.append(unit)
                continue
            short11 = img[e:e + 11].decode("ascii", "replace").rstrip()
            name = "".join(lfn_parts[::-1]) if lfn_parts else _short_to_name(short11)
            lfn_parts = []
            # '.' points at this same directory and '..' at its parent, so
            # recursing into them re-enters directories already walked and
            # never terminates.  They carry no payload; skip them here.
            if name in (".", ".."):
                continue
            clus = struct.unpack_from("<H", img, e + 26)[0]
            size = struct.unpack_from("<I", img, e + 28)[0]
            path = prefix + name
            found.append((path, attr, clus, size))
        return found

    found_files = {}
    stack = [(root_off, root_entries, "/")]
    visited = set()
    while stack:
        off, limit, prefix = stack.pop()
        # A corrupt or self-referencing directory could otherwise loop forever;
        # each directory region is walked at most once.
        key = (off, prefix)
        if key in visited:
            continue
        visited.add(key)
        for path, attr, clus, size in walk(off, limit, prefix):
            if attr & 0x10:
                if clus >= 2:
                    stack.append((data_off + (clus - 2) * cluster,
                                  cluster // 32, path + "/"))
                continue
            found_files[path.lstrip("/")] = (clus, size)

    problems = []
    for name, (clus, size) in sorted(found_files.items()):
        if name not in expected:
            problems.append("unexpected file in image: /%s" % name)
            continue
        want = expected[name]
        got = read_chain(clus, size)
        if size != len(want):
            problems.append("/%s size %d != %d" % (name, size, len(want)))
        elif got != want:
            diff = next((i for i in range(len(want)) if got[i] != want[i]), None)
            problems.append("/%s content differs (first byte %s)" % (name, diff))
    for name in sorted(expected):
        if name not in found_files:
            problems.append("MISSING from image: /%s" % name)

    if problems:
        raise BuildError("verify failed (%d):\n  " % len(problems) + "\n  ".join(problems))

    # The kernel must still carry a valid multiboot2 header, or GRUB rejects it.
    kclus, ksize = found_files["EFI/nefuos/nefuos.bin"]
    kdata = read_chain(kclus, ksize)
    magic = struct.pack("<I", 0xE85250D6)
    off = -1
    for o in range(0, min(32768, len(kdata)) - 8, 8):
        if kdata[o:o + 4] == magic and struct.unpack_from("<I", kdata, o + 4)[0] == 0:
            hl = struct.unpack_from("<I", kdata, o + 8)[0]
            ck = struct.unpack_from("<I", kdata, o + 12)[0]
            if (0xE85250D6 + 0 + hl + ck) & 0xFFFFFFFF == 0:
                off = o
                break
    if off < 0:
        raise BuildError("verify: kernel multiboot2 header not found/invalid")

    print("  verify: %d files re-read byte-for-byte, all match" % len(found_files))
    print("  verify: kernel multiboot2 header valid at offset 0x%x (%d bytes)"
          % (off, ksize))
    if found_files.get("EFI/BOOT/BOOTX64.EFI") is None:
        raise BuildError("verify: /EFI/BOOT/BOOTX64.EFI missing — firmware cannot launch GRUB")
    return len(found_files)


def build(files, out_path):
    """Lay the files out into a FAT16 image.  Two passes: every file's bytes are
    allocated first (recording start cluster + size), then the directory entries
    are built bottom-up, because a parent's entries reference the clusters its
    children just claimed."""
    total = sum(len(v) for v in files.values())
    spc, fat_sectors, data_clusters, vol_sectors = pick_geometry(total, dir_count=7)
    img = bytearray(vol_sectors * SECTOR)
    fs = FatFS(img, spc, fat_sectors, data_clusters)

    img[0:SECTOR] = fat_bpb(vol_sectors, fat_sectors, spc)

    # ---- pass 1: allocate file data ----
    # Sorted so the layout (and therefore the image bytes) is reproducible.
    placed = {}
    for name in sorted(files):
        start = fs.write_file(files[name])
        placed[name] = (start, len(files[name]))

    # ---- pass 2: build the directory tree ----
    root = FatDir(fs, "/", is_root=True)
    efi = FatDir(fs, "EFI")
    boot = FatDir(fs, "BOOT")
    debian = FatDir(fs, "debian")
    nefuos = FatDir(fs, "nefuos")
    bootmod = FatDir(fs, "x86_64-efi")
    debmod = FatDir(fs, "x86_64-efi")
    for d in (efi, boot, debian, nefuos, bootmod, debmod):
        d.add(".", 0x10, 0, 0)
        d.add("..", 0x10, 0, 0)

    dirs = {
        "EFI/BOOT/x86_64-efi/": bootmod,
        "EFI/debian/x86_64-efi/": debmod,
        "EFI/BOOT/": boot,
        "EFI/debian/": debian,
        "EFI/nefuos/": nefuos,
    }
    for name in sorted(placed):
        start, sz = placed[name]
        dirname, leaf = name.rsplit("/", 1)
        target = dirs.get(dirname + "/")
        if target is None:
            raise BuildError("unhandled file path " + name)
        target.add(leaf, 0x20, start, sz)

    # Children finalize before parents so the parent records their clusters.
    bootmod.finalize(boot)
    debmod.finalize(debian)
    boot.finalize(efi)
    debian.finalize(efi)
    nefuos.finalize(efi)
    efi.finalize(root)
    root.finalize(None)

    bootmod.patch_dotdot(boot.start)
    debmod.patch_dotdot(debian.start)
    boot.patch_dotdot(efi.start)
    debian.patch_dotdot(efi.start)
    nefuos.patch_dotdot(efi.start)
    efi.patch_dotdot(ROOT_CLUSTER)

    # ---- FAT tables ----
    fat_bytes = b"".join(struct.pack("<H", v & 0xFFFF) for v in fs.fat)
    fat_bytes = fat_bytes.ljust(fat_sectors * SECTOR, b"\x00")
    fat_off1 = RESERVED * SECTOR
    fat_off2 = fat_off1 + fat_sectors * SECTOR
    img[fat_off1:fat_off1 + len(fat_bytes)] = fat_bytes
    img[fat_off2:fat_off2 + len(fat_bytes)] = fat_bytes

    with open(out_path, "wb") as f:
        f.write(img)

    print("EFI boot image OK: %s (%d bytes, %.1f MiB)"
          % (out_path, len(img), len(img) / 1048576))
    print("  FAT16: %d sec/cluster (%d B), vol=%d sectors, %d sec/FAT, "
          "%d data clusters, %d free"
          % (spc, spc * SECTOR, vol_sectors, fat_sectors, data_clusters, fs.free))
    print("  payload: %d files, %d bytes" % (len(files), total))

    # Independent re-read: never hand back an image we have not parsed back.
    verify_image(img, files)
    return out_path


def main():
    if len(sys.argv) != 6:
        print("usage: make_efi_img.py <grubx64.efi> <grub.cfg> <kernel.bin> <modules_dir> <out.img>")
        return 2
    efi_path, cfg_path, kernel_path, mods_dir, out_path = sys.argv[1:6]

    for p in (efi_path, cfg_path, kernel_path):
        if not os.path.exists(p):
            print("ERROR: missing input %s" % p)
            return 1
    if not os.path.isdir(mods_dir):
        print("ERROR: modules dir not found: %s" % mods_dir)
        return 1

    mods = module_closure(mods_dir)
    if not mods:
        print("ERROR: no GRUB modules resolved from %s (moddep.lst missing?)" % mods_dir)
        return 1
    if "multiboot2" not in mods:
        print("ERROR: multiboot2.mod is not in the closure — GRUB cannot load the kernel")
        return 1

    files = {}
    files["EFI/BOOT/BOOTX64.EFI"] = open(efi_path, "rb").read()
    cfg = open(cfg_path, "rb").read()
    files["EFI/BOOT/grub.cfg"] = cfg
    files["EFI/debian/grub.cfg"] = cfg
    files["EFI/nefuos/nefuos.bin"] = open(kernel_path, "rb").read()

    mod_bytes = 0
    for m in mods:
        with open(os.path.join(mods_dir, m + ".mod"), "rb") as f:
            data = f.read()
        files["EFI/BOOT/x86_64-efi/" + m + ".mod"] = data
        files["EFI/debian/x86_64-efi/" + m + ".mod"] = data
        mod_bytes += len(data)
    print("modules staged: %d (%d bytes per prefix, %d bytes both)"
          % (len(mods), mod_bytes, mod_bytes * 2))

    build(files, out_path)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BuildError as e:
        print("ERROR: %s" % e)
        sys.exit(1)
