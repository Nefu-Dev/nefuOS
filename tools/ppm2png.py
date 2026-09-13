import struct, zlib, sys

src = r"D:\mycppos1\nefuOS\build\bare\screen1.ppm"
dst = r"D:\mycppos1\nefuOS\build\bare\screen1.png"

f = open(src, "rb")
assert f.readline().strip() == b"P6"
w, h = [int(x) for x in f.readline().split()]
f.readline()
data = f.read()
f.close()

def chunk(t, d):
    out.write(struct.pack(">I", len(d)))
    out.write(t)
    out.write(d)
    out.write(struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF))

out = open(dst, "wb")
out.write(b"\x89PNG\r\n\x1a\n")
chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
raw = b""
stride = w * 3
for y in range(h):
    raw += b"\x00" + data[y * stride:(y + 1) * stride]
chunk(b"IDAT", zlib.compress(raw))
chunk(b"IEND", b"")
out.close()
print("ok", w, h)
