import struct
import sys

path = sys.argv[1]
outpath = sys.argv[2]
data = open(path, 'rb').read()
w, h = 1024, 768
pitch = 4096
# LFB is top-down BGRA; BMP wants bottom-up BGRA
lines = []
for y in range(h - 1, -1, -1):
    base = y * pitch
    lines.append(data[base:base + w * 4])
body = b''.join(lines)
file_size = 54 + len(body)
bmp = bytearray(b'BM')
bmp += struct.pack('<IHHI', file_size, 0, 0, 54)
bmp += struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, len(body), 2835, 2835, 0, 0)
bmp += body
open(outpath, 'wb').write(bmp)
print('bmp written', len(bmp))
