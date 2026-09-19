import struct, sys

def fb_to_bmp(fb, w, h, pitch, out, flip_y=True):
    with open(fb, 'rb') as f:
        data = f.read()
    row = w * 4
    bmp_row = (w * 3 + 3) & ~3
    img = bytearray(bmp_row * h)
    for y in range(h):
        src = y * pitch
        dst = (h - 1 - y if flip_y else y) * bmp_row
        for x in range(w):
            px = struct.unpack_from('<I', data, src + x * 4)[0]
            r = (px >> 16) & 0xFF
            g = (px >> 8) & 0xFF
            b = px & 0xFF
            img[dst + x * 3 + 0] = b
            img[dst + x * 3 + 1] = g
            img[dst + x * 3 + 2] = r
    pad = 0
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(img), 0, 0, 54)
    hdr += struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(img),
                       bmp_row, 0, 0, 0)
    with open(out, 'wb') as f:
        f.write(hdr)
        f.write(img)
    print('bmp written', out, w, 'x', h)

if __name__ == '__main__':
    fb_to_bmp(sys.argv[1], 1024, 768, 4096, sys.argv[2])
