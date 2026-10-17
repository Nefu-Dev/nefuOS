#!/usr/bin/env python3
# nefuOS: put host files into the nefuos.fs VFS snapshot (NFS1 format).
# Usage: python tools/nvfs_put.py <fs_path> <dst_path_in_fs> <host_file>
#   e.g. python tools/nvfs_put.py nefuos.fs /home/Collections.class tests/java/out/Collections.class
# Serialization matches core/vfs/vfs.cpp ser_node/deser_node (magic 0x4E465301).
import struct
import sys

def parse(b, pos, path):
    """Return (tree_node, next_pos). node: dict(kind,name,children|size,data)."""
    typ = b[pos]; pos += 1
    nl = struct.unpack("<H", b[pos:pos+2])[0]; pos += 2
    nm = b[pos:pos+nl].decode("utf-8", "replace"); pos += nl
    full = path + "/" + nm if path != "/" else "/" + nm
    if typ == 0:
        cc = struct.unpack("<H", b[pos:pos+2])[0]; pos += 2
        kids = []
        for _ in range(cc):
            kid, pos = parse(b, pos, full)
            kids.append(kid)
        return {"kind": "d", "name": nm, "children": kids}, pos
    sz = struct.unpack("<I", b[pos:pos+4])[0]; pos += 4
    data = b[pos:pos+sz]; pos += sz
    return {"kind": "f", "name": nm, "size": sz, "data": data}, pos

def serialize(node):
    out = bytearray()
    name = node["name"].encode("utf-8")
    out.append(0 if node["kind"] == "d" else 1)
    out += struct.pack("<H", len(name)) + name
    if node["kind"] == "d":
        kids = node["children"]
        out += struct.pack("<H", len(kids))
        for k in kids:
            out += serialize(k)
    else:
        out += struct.pack("<I", node["size"]) + node["data"]
    return bytes(out)

def find(node, parts):
    if not parts:
        return node
    head = parts[0]
    for c in node.get("children", []):
        if c["name"] == head:
            return find(c, parts[1:])
    return None

def put_file(root, parts, name, data):
    node = find(root, parts)
    if node is None or node["kind"] != "d":
        # create missing dirs
        cur = root
        for part in parts:
            nxt = None
            for c in cur.get("children", []):
                if c["name"] == part:
                    nxt = c
                    break
            if nxt is None:
                nxt = {"kind": "d", "name": part, "children": []}
                cur["children"].append(nxt)
            cur = nxt
        node = cur
    for c in node["children"]:
        if c["name"] == name and c["kind"] == "f":
            c["size"] = len(data)
            c["data"] = data
            return
    node["children"].append({"kind": "f", "name": name, "size": len(data), "data": data})

def main():
    if len(sys.argv) != 4:
        print("usage: nvfs_put.py <fs_path> <dst_path_in_fs> <host_file>")
        return 1
    fs_path, dst, host = sys.argv[1], sys.argv[2], sys.argv[3]
    b = open(fs_path, "rb").read()
    assert b[:4] == b"\x01SFN", "not an NFS1 snapshot: %r" % b[:8]
    root, _ = parse(b, 4, "/")
    data = open(host, "rb").read()
    parts = [p for p in dst.split("/") if p]
    put_file(root, parts[:-1], parts[-1], data)
    out = b[:4] + serialize(root)
    open(fs_path, "wb").write(out)
    print("put %s (%d bytes) into %s" % (dst, len(data), fs_path))
    return 0

if __name__ == "__main__":
    sys.exit(main())
