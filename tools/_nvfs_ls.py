import struct, sys

def parse(fs_path, want_path="/usr/share/apps"):
    with open(fs_path, "rb") as fh:
        data = fh.read()
    parse_bytes(data, want_path)

def parse_bytes(data, want_path="/usr/share/apps"):
    assert data[:4] in (b"NVFS", b"SFVN"), data[:8]
    magic = struct.unpack_from("<I", data, 0)[0]
    assert magic == 0x4E564653, hex(magic)
    sb = struct.unpack_from("<IIIIIIIII", data, 0)
    magic, version, blksz, total, free, root_ino, jstart, jblocks, ud_start = sb
    print("NVFS %r ver=%d blocks=%d free=%d root_ino=%d" % (data[:4], version, total, free, root_ino))

    def inode(ino):
        off = 512 + ino * 111
        (inum, typ, mode, size) = struct.unpack_from("<IBHI", data, off)
        blocks = list(struct.unpack_from("<12I", data, off + 11))
        ind = struct.unpack_from("<I", data, off + 59)[0]
        dbl = struct.unpack_from("<I", data, off + 63)[0]
        return {"type": typ, "size": size, "blocks": blocks, "ind": ind, "dbl": dbl}

    def read_inode_data(ino):
        n = inode(ino)
        out = b""
        for b in n["blocks"]:
            if b:
                out += data[b*512:(b+1)*512]
        if n["ind"]:
            tbl = struct.unpack_from("<128I", data, n["ind"]*512)
            for b in tbl:
                if b:
                    out += data[b*512:(b+1)*512]
        if n["dbl"]:
            tbl1 = struct.unpack_from("<128I", data, n["dbl"]*512)
            for b1 in tbl1:
                if not b1: continue
                tbl2 = struct.unpack_from("<128I", data, b1*512)
                for b2 in tbl2:
                    if b2:
                        out += data[b2*512:(b2+1)*512]
        return out[:n["size"]]

    def ls(ino):
        raw = read_inode_data(ino)
        entries = []
        pos = 0
        while pos + 68 <= len(raw):
            (inode_no, rec_len, name_len, typ) = struct.unpack_from("<IHBB", raw, pos)
            name = raw[pos+8:pos+8+name_len].decode("ascii", "replace")
            entries.append((name, inode_no, typ))
            pos += rec_len if rec_len else 68
            if rec_len == 0: break
        return entries

    # walk from root
    cur = root_ino
    for comp in [c for c in want_path.split("/") if c]:
        entries = ls(cur)
        found = None
        for (name, ino, typ) in entries:
            if name == comp:
                found = (ino, typ)
                break
        if not found:
            print("path component not found:", comp)
            return
        cur, typ = found
        print("component %s -> inode %d type %d" % (comp, cur, typ))

    # list target dir
    for (name, ino, typ) in ls(cur):
        if typ == 1:
            n = inode(ino)
            print("  FILE %-40s inode=%d size=%d" % (name, ino, n["size"]))
        elif typ == 2:
            print("  DIR  %-40s inode=%d" % (name, ino))
        else:
            print("  ?    %-40s inode=%d type=%d" % (name, ino, typ))

if __name__ == "__main__":
    parse(sys.argv[1] if len(sys.argv) > 1 else r"D:\mycppos1\nefuOS\nefuos.fs")
