import sys
from PIL import Image

def diff(a, b, box=None, name=""):
    ia = Image.open(a).convert("RGB")
    ib = Image.open(b).convert("RGB")
    if ia.size != ib.size:
        print(f"{name}: SIZE MISMATCH {ia.size} vs {ib.size}")
        return None
    wa, ha = ia.size
    pa = ia.load(); pb = ib.load()
    y0 = box[1] if box else 0
    y1 = box[3] if box else ha
    x0 = box[0] if box else 0
    x1 = box[2] if box else wa
    n = 0
    rows = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pa[x, y] != pb[x, y]:
                n += 1
                rows[y] = rows.get(y, 0) + 1
    print(f"{name}: differing pixels={n}")
    if rows:
        top = sorted(rows.items(), key=lambda kv: -kv[1])[:8]
        print(f"  top diff rows: {top}")
    return n

# 14 vs 16: both Inbox list, only status bar text should differ.
# status bar strip is at window y ~609..627; compare everything above it.
d1 = diff(r"D:\mycppos1\nefuOS\shots\14_list_c.png",
          r"D:\mycppos1\nefuOS\shots\16_list_d.png",
          box=(0, 0, 806, 600), name="14_vs_16 (above status bar)")

# 10 vs 12: expected diffs only at inbox badge (3->2) and row0 color.
d2 = diff(r"D:\mycppos1\nefuOS\shots\10_list_a.png",
          r"D:\mycppos1\nefuOS\shots\12_list_b.png",
          box=(0, 0, 806, 600), name="10_vs_12 (above status bar)")

# 10 vs 14: expected diffs at badge/row0 plus status bar; check body above status only.
d3 = diff(r"D:\mycppos1\nefuOS\shots\10_list_a.png",
          r"D:\mycppos1\nefuOS\shots\14_list_c.png",
          box=(0, 0, 806, 600), name="10_vs_14 (above status bar)")
