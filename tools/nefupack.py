#!/usr/bin/env python3
# nefuOS nefupack - package builder for .nefud manifests and NEFBIN01 .bin apps.
# Usage:
#   nefupack.py make-nefud <name> <desc> <author> <ver> <out.nefud>
#   nefupack.py make-bin   <name> <desc> <script|bytecode-file> <out.bin>
# The .bin header is a fixed 64-byte record; payload is appended as-is.
import sys, os

NEFBIN01 = b"NEFBIN01"

def make_nefud(name, desc, author, ver, out):
    content = "name=%s\ndesc=%s\nauthor=%s\nver=%s\n" % (name, desc, author, ver)
    with open(out, "w", encoding="utf-8") as f:
        f.write(content)
    print("nefupack: wrote %s (%d bytes, .nefud manifest)" % (out, len(content.encode())))

def make_bin(name, desc, payload_path, out):
    with open(payload_path, "rb") as f:
        payload = f.read()
    if len(name) > 24:  name = name[:24]
    if len(desc) > 32:  desc = desc[:32]
    hdr = bytearray(64)
    hdr[0:8] = NEFBIN01
    hdr[8:8+len(name)] = name.encode("utf-8")
    hdr[32:32+len(desc)] = desc.encode("utf-8")
    data = bytes(hdr) + payload
    with open(out, "wb") as f:
        f.write(data)
    print("nefupack: wrote %s (%d bytes, NEFBIN01 package)" % (out, len(data)))

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "make-nefud" and len(sys.argv) == 7:
        make_nefud(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
    elif cmd == "make-bin" and len(sys.argv) == 6:
        make_bin(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        print("nefupack: bad arguments")
        print(__doc__)
        sys.exit(1)

if __name__ == "__main__":
    main()
