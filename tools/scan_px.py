#!/usr/bin/env python3
"""Scan a BMP screenshot for non-background pixels and report the dominant colours
plus their bounding boxes. Used to judge WebGL probes without guessing coordinates.

Usage: python tools/scan_px.py <bmp> [bgR,bgG,bgB] [minPixels]
"""
import sys, struct
from collections import Counter


def load_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 32:
        raise SystemExit('unsupported bpp=%d' % bpp)
    top_down = h < 0
    h = abs(h)
    stride = w * 4
    rows = []
    for y in range(h):
        ry = y if top_down else (h - 1 - y)
        base = off + ry * stride
        rows.append(data[base:base + stride])
    return w, h, rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    bg = (0, 0, 0)
    if len(sys.argv) > 2:
        bg = tuple(int(v) for v in sys.argv[2].split(','))
    minpx = int(sys.argv[3]) if len(sys.argv) > 3 else 200

    w, h, rows = load_bmp(path)
    cnt = Counter()
    boxes = {}
    for y in range(h):
        row = rows[y]
        for x in range(w):
            r, g, b = row[x * 4 + 2], row[x * 4 + 1], row[x * 4]
            # quantise so anti-aliasing does not fragment the histogram
            key = (r // 16 * 16, g // 16 * 16, b // 16 * 16)
            if abs(r - bg[0]) < 12 and abs(g - bg[1]) < 12 and abs(b - bg[2]) < 12:
                continue
            cnt[key] += 1
            bx = boxes.get(key)
            if bx is None:
                boxes[key] = [x, y, x, y]
            else:
                if x < bx[0]: bx[0] = x
                if y < bx[1]: bx[1] = y
                if x > bx[2]: bx[2] = x
                if y > bx[3]: bx[3] = y

    print('%s %dx%d  distinct=%d' % (path, w, h, len(cnt)))
    for key, n in cnt.most_common(8):
        if n < minpx:
            continue
        bx = boxes[key]
        print('  rgb=(%3d,%3d,%3d) px=%-7d box=%d,%d..%d,%d' % (key[0], key[1], key[2], n, bx[0], bx[1], bx[2], bx[3]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
