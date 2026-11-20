# -*- coding: utf-8 -*-
"""Convert a nefuOS --shot .bmp/.bin framebuffer into a viewable .png.

Usage:
  python tools\shot2png.py <src.bmp> <dst.png> [max_w] [crop_w] [crop_h]

The framebuffer dump is BGRA/XRGB in file order; PIL opens the .bmp written
by lfb2bmp/native saver directly. max_w scales the long edge down so the
result stays under the multimodal image limit, crop_* optionally cuts the
top-left window area first.
"""
import sys, os
from PIL import Image


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    max_w = int(sys.argv[3]) if len(sys.argv) > 3 else 1400
    cw = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    chh = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    im = Image.open(src).convert("RGB")
    if cw and chh:
        im = im.crop((0, 0, min(cw, im.width), min(chh, im.height)))
    if im.width > max_w:
        h = int(im.height * max_w / im.width)
        im = im.resize((max_w, h), Image.LANCZOS)
    im.save(dst, optimize=True)
    print("%s %dx%d -> %s" % (src, im.width, im.height, dst))
    return 0


if __name__ == "__main__":
    sys.exit(main())
