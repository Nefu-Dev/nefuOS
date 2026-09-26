from PIL import Image

im = Image.open(r"D:\mycppos1\nefuOS\shots\p0_list.png").convert("RGB")
w, h = im.size
px = im.load()
target = (44, 62, 80)
cols = []
for x in range(w):
    cnt = 0
    for y in range(100, 560, 2):
        if px[x, y] == target:
            cnt += 1
    cols.append(cnt)
runs = []; inrun = False
for x in range(w):
    if cols[x] > 100 and not inrun:
        start = x; inrun = True
    elif cols[x] <= 100 and inrun:
        runs.append((start, x - 1)); inrun = False
if inrun: runs.append((start, w - 1))
print("sidebar runs:", runs)
if runs:
    x0, x1 = runs[0]
    top = None
    for y in range(h):
        if px[x0 + 5, y] == target:
            top = y; break
    print("sidebar top:", top)
    # where is the Account row text? content y 218 (52+5*32+6) -> client y = top + 218 - ??
    # The folder text rows: Inbox at content y 58. Find text pixels (ECF0F1 on 2C3E50) in the sidebar
    # to calibrate: scan for light text rows in sidebar column region
    light = (236, 240, 241)
    rows = {}
    for y in range(top, top + 400):
        c = 0
        for x in range(x0 + 4, x1 - 4, 2):
            if px[x, y] == light:
                c += 1
        if c > 3:
            rows[y] = c
    # cluster consecutive y rows into text lines
    lines = []
    cur = None
    for y in sorted(rows):
        if cur is None or y > cur[1] + 2:
            if cur: lines.append(cur)
            cur = [y, y]
        else:
            cur[1] = y
    if cur: lines.append(cur)
    print("sidebar text lines (client y ranges):")
    for ln in lines:
        print("  ", ln)
