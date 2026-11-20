#!/usr/bin/env python3
"""Read a BMP screenshot produced by the nefuOS shot loop and report the colour at
given coordinates, so WebGL probes can be judged by pixels instead of by eye.

Usage: python tools/probe_px.py <bmp> x1,y1 x2,y2 ...
Coordinates are in the bitmap's own space (top-left origin, typical 1000x800).
"""
import sys, struct


def load_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    comp = struct.unpack_from('<I', data, 30)[0]
    if bpp != 32 or comp not in (0, 3):
        raise SystemExit('unsupported BMP bpp=%d comp=%d' % (bpp, comp))
    top_down = h < 0
    h = abs(h)
    stride = w * 4
    px = []
    for y in range(h):
        row_y = y if top_down else (h - 1 - y)
        base = off + row_y * stride
        row = bytearray(stride)
        row[:] = data[base:base + stride]
        px.append(row)
    return w, h, px


def sample(w, h, px, x, y):
    if not (0 <= x < w and 0 <= y < h):
        return None
    b, g, r, a = px[y][x * 4:x * 4 + 4]
    return (r, g, b, a)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    path = sys.argv[1]
    w, h, px = load_bmp(path)
    print('%s %dx%d' % (path, w, h))
    for spec in sys.argv[2:]:
        x, y = [int(v) for v in spec.split(',')]
        rgb = sample(w, h, px, x, y)
        if rgb is None:
            print('  (%d,%d) out of range' % (x, y))
        else:
            print('  (%4d,%4d) rgb=%3d,%3d,%3d a=%3d' % (x, y, rgb[0], rgb[1], rgb[2], rgb[3]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
