#!/usr/bin/env python3
# Minimal VNC (RFB) screenshot client for nefuOS bare-metal verification.
# Connects to QEMU's built-in VNC server, requests a full framebuffer update
# and writes a 32bpp BMP.
import socket, struct, sys, time

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 5999
out = sys.argv[3] if len(sys.argv) > 3 else "shot.bmp"

s = socket.create_connection((host, port), timeout=10)

def recvn(n):
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c: raise IOError("conn closed")
        buf += c
    return buf

# RFB handshake
ver = recvn(12)
assert ver.startswith(b"RFB "), ver
s.sendall(b"RFB 003.008\n")
sec = recvn(1)
if sec == b"\x01":  # none
    pass
else:
    # security types list (003.008)
    n = sec[0]
    types = recvn(n)
    if b"\x01" in types:
        s.sendall(b"\x01")
    else:
        raise IOError("no None security")
recvn(4)  # SecurityResult
s.sendall(b"NEFU"[:4])  # client name (padded)
# ServerInit
w, h = struct.unpack(">HH", recvn(4))
recvn(16)  # server pixel format

# SetPixelFormat: 32bpp BGRA
pf = struct.pack(">BBBBHHHHHHI", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0, 0)
s.sendall(b"\x00" + pf)
# SetEncodings: raw
s.sendall(b"\x02" + struct.pack(">H", 1) + struct.pack(">i", 0))
# FramebufferUpdateRequest (incremental=0, full)
s.sendall(b"\x03\x00" + struct.pack(">HHHH", 0, 0, w, h))

# read update
msgtype = recvn(1)
assert msgtype == b"\x00", msgtype
recvn(1)
nrects = struct.unpack(">H", recvn(2))[0]
data = b""
for _ in range(nrects):
    x, y, rw, rh, enc = struct.unpack(">HHHHi", recvn(12))
    if enc == 0:
        data += recvn(rw * rh * 4)
    else:
        raise IOError("unexpected encoding %d" % enc)

# write BMP (32bpp BGRA -> BMP BGRX)
row = w * 4
pad = (4 - (row % 4)) % 4
filesz = 54 + (row + pad) * h
bmp = bytearray()
bmp += b"BM" + struct.pack("<I", filesz) + b"\x00\x00\x00\x00" + struct.pack("<I", 54)
bmp += struct.pack("<I", 40) + struct.pack("<i", w) + struct.pack("<i", h)
bmp += struct.pack("<HH", 1, 32) + struct.pack("<I", 0) + struct.pack("<I", (row + pad) * h)
bmp += struct.pack("<iiii", 2835, 2835, 0, 0)
for y in range(h - 1, -1, -1):
    line = data[y * row:(y + 1) * row]
    bmp += line[:row] + b"\x00" * pad
open(out, "wb").write(bmp)
print("saved %dx%d -> %s (%d bytes)" % (w, h, out, len(bmp)))
