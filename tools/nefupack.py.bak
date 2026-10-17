#!/usr/bin/env python3
# nefupack - nefuOS application package tool.
# Builds / inspects .nefud packages and .bin (NEFBIN01) packages.
#
# .nefud format (text manifest + optional payload):
#   NEFUD1\n
#   name=AppName
#   desc=One line description
#   author=Who
#   ver=1.0
#   entry=builtin_app_name        (optional: binds to a built-in app)
#   @@PAYLOAD@@\n
#   <arbitrary payload bytes>
#   <8 byte CRC32 of everything above, big-endian>
#
# .bin format (NEFBIN01, NEFVM bytecode or native blob):
#   NEFBIN01 + 24B name + 32B desc + payload...
#   (payload is NEFVM bytecode that the nefuOS VM executes)
#
# Usage:
#   python nefupack.py pack <manifest.txt> [payload.bin] <out.nefud>
#   python nefupack.py unpack <file.nefud|file.bin> [outdir]
#   python nefupack.py info  <file.nefud|file.bin>
import sys, os, zlib, struct

MAGIC = b"NEFUD1"
BIN_MAGIC = b"NEFBIN01"

def crc32_be(data: bytes) -> bytes:
    return struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)

def pack_nefud(manifest_path, payload_path, out_path):
    with open(manifest_path, "r", encoding="utf-8-sig", errors="replace") as f:
        text = f.read()
    lines = []
    for ln in text.splitlines():
        ln = ln.rstrip("\r")
        if not ln:
            continue
        if "=" not in ln:
            continue
        key, _, val = ln.partition("=")
        key = key.strip()
        val = val.strip()
        if key in ("name", "desc", "author", "ver", "entry", "icon"):
            lines.append("%s=%s" % (key, val))
    if not any(l.startswith("name=") for l in lines):
        print("nefupack: manifest must contain name=")
        sys.exit(1)
    head = MAGIC + b"\n" + "\n".join(lines).encode("utf-8") + b"\n"
    payload = b""
    if payload_path:
        with open(payload_path, "rb") as f:
            payload = f.read()
        if len(payload) > 4 * 1024 * 1024:
            print("nefupack: payload > 4MB, refusing")
            sys.exit(1)
    body = head + b"@@PAYLOAD@@\n" + payload
    out = body + crc32_be(body)
    with open(out_path, "wb") as f:
        f.write(out)
    print("nefupack: %s -> %s (%d bytes, payload %d)" %
          (manifest_path, out_path, len(out), len(payload)))

def parse_nefud(data: bytes):
    if data.startswith(BIN_MAGIC):
        name = data[8:32].split(b"\x00")[0].decode("utf-8", "replace")
        desc = data[32:64].split(b"\x00")[0].decode("utf-8", "replace")
        print("format: NEFBIN01 (.bin)")
        print("name  : %s" % name)
        print("desc  : %s" % desc)
        print("size  : %d bytes" % len(data))
        return
    if not data.startswith(MAGIC):
        print("nefupack: not a nefuOS package (no NEFUD1/NEFBIN01 magic)")
        sys.exit(2)
    # verify CRC if present (trailing 4-byte big-endian CRC32)
    if len(data) >= 8:
        body, crc = data[:-4], data[-4:]
        if crc32_be(body) == crc:
            print("crc32 : OK")
        else:
            print("crc32 : MISMATCH (payload corrupt?)")
    pos = data.index(b"\n") + 1
    mark = data.find(b"@@PAYLOAD@@\n", pos)
    if mark < 0:
        head = data[pos:]
        payload = b""
    else:
        head = data[pos:mark]
        payload = data[mark + len(b"@@PAYLOAD@@\n"):-4]
    for ln in head.split(b"\n"):
        if ln:
            print(ln.decode("utf-8", "replace"))
    print("payload: %d bytes" % len(payload))

def unpack(data, outdir):
    if data.startswith(BIN_MAGIC):
        payload = data[64:]
        name = data[8:32].split(b"\x00")[0].decode("utf-8", "replace") or "app"
        os.makedirs(outdir, exist_ok=True)
        with open(os.path.join(outdir, name + ".bin.payload"), "wb") as f:
            f.write(payload)
        print("unpacked .bin payload -> %s" % outdir)
        return
    mark = data.find(b"@@PAYLOAD@@\n")
    if mark < 0:
        print("no payload section")
        return
    payload = data[mark + len(b"@@PAYLOAD@@\n"):-4]
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "payload.bin"), "wb") as f:
        f.write(payload)
    print("unpacked payload -> %s/payload.bin (%d bytes)" % (outdir, len(payload)))

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    mode = sys.argv[1]
    if mode == "pack":
        if len(sys.argv) not in (4, 5):
            print("usage: nefupack.py pack <manifest.txt> [payload.bin] <out.nefud>")
            sys.exit(1)
        if len(sys.argv) == 4:
            pack_nefud(sys.argv[2], None, sys.argv[3])
        else:
            pack_nefud(sys.argv[2], sys.argv[3], sys.argv[4])
    elif mode == "info":
        with open(sys.argv[2], "rb") as f:
            parse_nefud(f.read())
    elif mode == "unpack":
        with open(sys.argv[2], "rb") as f:
            unpack(f.read(), sys.argv[3] if len(sys.argv) > 3 else ".")
    else:
        print("unknown mode: %s" % mode)
        sys.exit(1)

if __name__ == "__main__":
    main()
