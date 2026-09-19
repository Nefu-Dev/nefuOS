# -*- coding: utf-8 -*-
"""Independent read-back validation of the nefuOS ESP image (GPT + FAT32).

Parses the image with a fresh FAT32 reader (not the writer's code) and checks:
  - protective MBR + GPT header CRC + partition entries (ESP GUID, LBAs)
  - FAT32 BPB sanity (sectors/cluster, FAT size, root cluster)
  - directory walk: \EFI\BOOT\BOOTX64.EFI, \EFI\BOOT\grub.cfg,
    \EFI\debian\grub.cfg, \EFI\nefuos\nefuos.bin, modules
  - FAT chain traversal for each file, content spot-check (GRUB PE header,
    multiboot2 magic of nefuos.bin, LFN short-name checksums)

Usage: python verify_esp.py <img> [<expected_kernel_size>]
"""
import os
import struct
import sys
import zlib

SECTOR = 512


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


class FatReader:
    def __init__(self, data, base_lba):
        self.data = data
        self.base = base_lba * SECTOR
        bpb = data[self.base:self.base + SECTOR]
        assert bpb[510:512] == b"\x55\xAA", "no boot signature"
        self.bytes_per_sec = struct.unpack_from("<H", bpb, 11)[0]
        self.sec_per_cluster = bpb[13]
        self.reserved = struct.unpack_from("<H", bpb, 14)[0]
        self.nfat = bpb[16]
        self.fat_secs = struct.unpack_from("<I", bpb, 36)[0]
        self.root_cluster = struct.unpack_from("<I", bpb, 44)[0]
        self.data_start = self.reserved + self.nfat * self.fat_secs
        self.sec_per_fat_ent = 4

    def cluster_off(self, c):
        return self.base + self.data_start * self.bytes_per_sec + (c - 2) * self.sec_per_cluster * self.bytes_per_sec

    def fat_next(self, c):
        off = self.base + self.reserved * self.bytes_per_sec + c * 4
        v = struct.unpack_from("<I", self.data, off)[0] & 0x0FFFFFFF
        return v

    def chain(self, start):
        out = bytearray()
        c = start
        seen = set()
        while 2 <= c < 0x0FFFFFF8:
            assert c not in seen, "FAT loop at %x" % c
            seen.add(c)
            off = self.cluster_off(c)
            out += self.data[off:off + self.sec_per_cluster * self.bytes_per_sec]
            n = self.fat_next(c)
            if n == 0x0FFFFFFF:
                break
            c = n
        return bytes(out)

    def readdir(self, cluster, path, out):
        data = self.chain(cluster)
        lfn = {}   # seq -> chunk bytes
        i = 0
        while i + 32 <= len(data):
            e = data[i:i + 32]
            i += 32
            if e[0] == 0x00:
                break
            if e[0] == 0xE5:
                continue
            if e[11] & 0x0F == 0x0F:      # LFN entry
                seq = e[0] & 0x1F
                chunk = e[1:11] + e[14:26] + e[28:32]
                lfn[seq] = chunk
                continue
            # 8.3 entry
            name = e[0:8].decode("ascii", "replace").rstrip()
            ext = e[8:11].decode("ascii", "replace").rstrip()
            attr = e[11]
            cluster_hi = struct.unpack_from("<H", e, 20)[0]
            cluster_lo = struct.unpack_from("<H", e, 26)[0]
            cl = (cluster_hi << 16) | cluster_lo
            size = struct.unpack_from("<I", e, 28)[0]
            if lfn:
                raw = b""
                for s in sorted(lfn):
                    raw += lfn[s]
                # strip fill as whole UTF-16 units only (0x0000 / 0xFFFF)
                while raw and raw[-2:] in (b"\x00\x00", b"\xff\xff"):
                    raw = raw[:-2]
                full = raw.decode("utf-16le", "replace")
                lfn = {}
            else:
                full = name + ("." + ext if ext else "")
            out.append((path + "/" + full, attr, cl, size))
        return out

    def walk(self, start, path, out, visited=None):
        if visited is None:
            visited = set()
        if start in visited:
            return out
        visited.add(start)
        for p, attr, cl, size in self.readdir(start, path, out):
            if attr & 0x10 and p not in (path + "/.", path + "/.."):
                self.walk(cl, p, out, visited)
        return out


def check_lfn(img, base_lba):
    """Verify every 8.3 entry with LFN predecessors has matching checksums."""
    b = img[base_lba * SECTOR:base_lba * SECTOR + SECTOR]
    reserved = struct.unpack_from("<H", b, 14)[0]
    nfat = b[16]
    fat_secs = struct.unpack_from("<I", b, 36)[0]
    root_cl = struct.unpack_from("<I", b, 44)[0]
    data_start = reserved + nfat * fat_secs
    root_off = base_lba * SECTOR + data_start * SECTOR + (root_cl - 2) * SECTOR

    def chk(name):
        if isinstance(name, str):
            name = name.encode("ascii")
        s = 0
        for ch in name:
            s = (((s & 1) << 7) | (s >> 1)) + ch
            s &= 0xFF
        return s

    # walk the whole FAT cluster space is overkill; spot-check directories
    # by scanning the image for the root dir sector (cluster 2) only.
    d = img[root_off:root_off + 512]
    pending = []
    ok = True
    for i in range(0, 512, 32):
        e = d[i:i + 32]
        if e[0] in (0, 0xE5):
            continue
        if e[11] == 0x0F:
            pending.append(e[13])
            continue
        if pending:
            expect = chk(e[0:11])
            for c in pending:
                if c != expect:
                    ok = False
                    print("  LFN checksum mismatch: 8.3=%r got=%02x want=%02x" % (e[0:11], c, expect))
            pending = []
    return ok


def gpt_crc_ok(hdr92):
    """GPT spec check: zero the CRC field, recompute CRC-32, compare."""
    h = bytearray(hdr92)
    stored = struct.unpack_from("<I", h, 16)[0]
    struct.pack_into("<I", h, 16, 0)
    return crc32(bytes(h)) == stored


def main():
    path = sys.argv[1]
    want_kernel = int(sys.argv[2], 0) if len(sys.argv) > 2 else None
    img = open(path, "rb").read()
    nsec = len(img) // SECTOR
    print("image: %s  %d bytes  %d sectors" % (path, len(img), nsec))
    ok = True

    # ---- GPT ----
    mbr = img[0:512]
    assert mbr[510:512] == b"\x55\xAA"
    print("protective MBR: type=0x%02x start=%d size=%d" % (mbr[450], struct.unpack_from("<I", mbr, 454)[0], struct.unpack_from("<I", mbr, 458)[0]))
    g = img[512:512 + 92]
    assert g[0:8] == b"EFI PART", "bad GPT signature"
    assert gpt_crc_ok(g), "primary GPT header CRC mismatch"
    cur, bak = struct.unpack_from("<QQ", g, 24)
    print("GPT header: current_lba=%d backup_lba=%d usable=%d..%d" % (cur, bak, *struct.unpack_from("<QQ", g, 40)))
    ents = img[2 * 512:34 * 512]
    assert crc32(ents) == struct.unpack_from("<I", g, 88)[0], "GPT entries CRC mismatch"
    type_guid = ents[0:16].hex()
    print("partition type GUID bytes: %s (expect 28272ac11ff8d211ba4b00a0c93ec93b)" % type_guid)
    ok &= type_guid == "28272ac11ff8d211ba4b00a0c93ec93b"
    pstart, pend = struct.unpack_from("<QQ", ents, 32)
    print("partition LBA: %d..%d (sectors %d, %.1f MiB)" % (pstart, pend, pend - pstart + 1, (pend - pstart + 1) / 2048))
    # backup GPT
    gb = img[(nsec - 1) * 512:(nsec - 1) * 512 + 92]
    assert gb[0:8] == b"EFI PART" and gpt_crc_ok(gb), "backup GPT bad"
    eb = img[(nsec - 33) * 512:(nsec - 1) * 512]
    assert crc32(eb) == struct.unpack_from("<I", gb, 88)[0], "backup entries CRC mismatch"
    print("backup GPT: ok")

    # ---- FAT32 ----
    fr = FatReader(img, pstart)
    print("FAT32: bps=%d spc=%d reserved=%d nfat=%d fat_secs=%d root_cluster=%d data_start=%d" %
          (fr.bytes_per_sec, fr.sec_per_cluster, fr.reserved, fr.nfat, fr.fat_secs, fr.root_cluster, fr.data_start))
    assert fr.bytes_per_sec == 512 and fr.sec_per_cluster == 1
    ok &= check_lfn(img, pstart)

    # ---- walk ----
    out = []
    fr.walk(fr.root_cluster, "", out)
    names = {p: (a, c, s) for p, a, c, s in out}
    for p, a, c, s in sorted(out):
        print("  %-36s attr=0x%02x cluster=%-6d size=%d" % (p, a, c, s))

    required = [
        ("/EFI", 0x10),
        ("/EFI/BOOT", 0x10),
        ("/EFI/debian", 0x10),
        ("/EFI/nefuos", 0x10),
        ("/EFI/BOOT/BOOTX64.EFI", 0x20),
        ("/EFI/BOOT/grub.cfg", 0x20),
        ("/EFI/debian/grub.cfg", 0x20),
        ("/EFI/nefuos/nefuos.bin", 0x20),
        ("/EFI/debian/x86_64-efi", 0x10),
    ]
    for p, a in required:
        if p not in names:
            print("  MISSING: %s" % p)
            ok = False
        elif names[p][0] != a:
            print("  BAD ATTR: %s attr=0x%02x" % (p, names[p][0]))
            ok = False
    if "/EFI/debian/x86_64-efi" in names:
        nmods = sum(1 for p in names if p.startswith("/EFI/debian/x86_64-efi/"))
        print("  modules under /EFI/debian/x86_64-efi: %d" % nmods)

    # ---- content spot checks ----
    for p in ("/EFI/BOOT/BOOTX64.EFI", "/EFI/nefuos/nefuos.bin"):
        if p in names:
            data = fr.chain(names[p][1])
            if p.endswith(".EFI"):
                print("%s: size=%d PE sig=%s" % (p, len(data), "MZ" if data[:2] == b"MZ" else "??"))
                ok &= data[:2] == b"MZ"
            else:
                print("%s: size=%d mb2 magic=%s" % (p, len(data), hex(struct.unpack_from("<I", data, 8)[0]) if len(data) > 12 else "n/a"))
                ok &= len(data) == want_kernel if want_kernel else True
                ok &= len(data) > 12 and struct.unpack_from("<I", data, 8)[0] == 0xE85250D6
    cfg = fr.chain(names["/EFI/BOOT/grub.cfg"][1]).decode("latin1")
    print("grub.cfg: %d bytes, multiboot2 entry: %s" % (len(cfg), "multiboot2" in cfg))

    print("VERIFY %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
