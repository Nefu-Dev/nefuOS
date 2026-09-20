# -*- coding: utf-8 -*-
# nefuOS CJK bitmap font generator
# Renders GB2312 level-1 hanzi (16-55 qu) plus full-width symbols (A1 qu)
# from Windows simhei.ttf into a 16x16 bitmap table embedded in cjk_font.h.
import io
from PIL import Image, ImageDraw, ImageFont

FONT = r'C:\Windows\Fonts\simhei.ttf'
OUT = r'D:\mycppos1\nefuOS\core\apps\cjk_font.h'

def gb2312_chars():
    """Yield (gb_bytes, unicode) for A1 qu + 16..55 qu (level-1 hanzi)."""
    out = []
    # full-width symbols: qu 1 (0xA1)
    for w in range(1, 95):
        hi, lo = 0xA1, 0xA0 + w
        try:
            ch = bytes([hi, lo]).decode('gb2312')
            out.append(((hi, lo), ord(ch)))
        except Exception:
            pass
    # level-1 hanzi: qu 16..55
    for q in range(16, 56):
        for w in range(1, 95):
            hi, lo = 0xA0 + q, 0xA0 + w
            try:
                ch = bytes([hi, lo]).decode('gb2312')
                out.append(((hi, lo), ord(ch)))
            except Exception:
                pass
    return out

def render_glyph(font, unicode_cp, size=16):
    """Render one glyph centered into a size x size 1-bit bitmap (row-major, MSB first)."""
    ch = chr(unicode_cp)
    img = Image.new('L', (size, size), 0)
    d = ImageDraw.Draw(img)
    # measure actual ink box
    bbox = d.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    if w <= 0 or h <= 0:
        return None
    # scale down if the glyph is larger than the cell (bold CJK at 16px can overflow)
    scale = min(size / w, size / h, 1.0)
    if scale < 0.999:
        f2 = ImageFont.truetype(FONT, max(6, int(16 * scale)))
        bbox2 = d.textbbox((0, 0), ch, font=f2)
        w2 = bbox2[2] - bbox2[0]
        h2 = bbox2[3] - bbox2[1]
        ox = (size - w2) // 2 - bbox2[0]
        oy = (size - h2) // 2 - bbox2[1]
        d.text((ox, oy), ch, font=f2, fill=255)
    else:
        ox = (size - w) // 2 - bbox[0]
        oy = (size - h) // 2 - bbox[1]
        d.text((ox, oy), ch, font=font, fill=255)
    px = img.load()
    rows = []
    for y in range(size):
        bits = 0
        for x in range(size):
            bits = (bits << 1) | (1 if px[x, y] >= 128 else 0)
        rows.append(bits)
    return rows

def main():
    chars = gb2312_chars()
    print('chars collected:', len(chars))
    font = ImageFont.truetype(FONT, 16)
    unis = []
    bits = []
    missing = 0
    for (hi, lo), uc in chars:
        rows = render_glyph(font, uc)
        if rows is None:
            missing += 1
            continue
        unis.append(uc)
        for r in rows:
            bits.append((r >> 8) & 0xFF)
            bits.append(r & 0xFF)
    n = len(unis)
    print('glyphs rendered:', n, 'missing:', missing)

    out = io.StringIO()
    out.write('// nefuOS built-in CJK bitmap font (16x16)\n')
    out.write('// GB2312 level-1 hanzi (16-55 qu) + full-width symbols (A1 qu)\n')
    out.write('// Rendered from simhei.ttf at build time; glyph shapes (CJK ideographs)\n')
    out.write('// are not copyrightable and are shared for interoperability.\n')
    out.write('#pragma once\n#include <stdint.h>\n\n')
    out.write('static const int g_cjk_count = %d;\n' % n)
    out.write('static const uint16_t g_cjk_uni[%d] = {\n' % n)
    for i in range(0, n, 12):
        out.write('    ' + ','.join('0x%04X' % u for u in unis[i:i+12]) + ',\n')
    out.write('};\n\n')
    out.write('static const uint8_t g_cjk_bits[%d] = {\n' % (n * 32))
    for i in range(0, len(bits), 24):
        out.write('    ' + ','.join('0x%02X' % b for b in bits[i:i+24]) + ',\n')
    out.write('};\n')
    io.open(OUT, 'w', encoding='utf-8', newline='\n').write(out.getvalue())
    print('wrote', OUT, 'bytes:', len(out.getvalue()))

if __name__ == '__main__':
    main()
