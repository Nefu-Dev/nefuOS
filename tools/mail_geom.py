from PIL import Image

# Find the email window geometry from a screenshot:
# the sidebar fill color is #2C3E50 (44,62,80) - a tall dark column right of the desktop icons.
im = Image.open(r"D:\mycppos1\nefuOS\shots\11_read.png").convert("RGB")
w, h = im.size
px = im.load()
target = (44, 62, 80)

# scan columns with a high density of the sidebar color in the vertical range 120..520
cols = []
for x in range(w):
    cnt = 0
    for y in range(120, 520, 2):
        if px[x, y] == target:
            cnt += 1
    cols.append(cnt)
# find contiguous runs
runs = []
inrun = False
for x in range(w):
    if cols[x] > 120 and not inrun:
        start = x; inrun = True
    elif cols[x] <= 120 and inrun:
        runs.append((start, x - 1)); inrun = False
if inrun:
    runs.append((start, w - 1))
print("sidebar column runs (x0,x1):", runs)

# Now find the top of the sidebar (window top) and the bottom.
if runs:
    x0, x1 = runs[0]
    # top: first y where the run's column has target color
    top = None
    for y in range(h):
        if px[x0 + 5, y] == target:
            top = y
            break
    bot = None
    for y in range(h - 1, 0, -1):
        if px[x0 + 5, y] == target:
            bot = y
            break
    print(f"window client: x0={x0} x1={x1} top={top} bot={bot}")
    # email window width 640 -> content x range [x0, x0+640), y range [top, top+460)
    print(f"email content: x {x0}..{x0+640}, y {top}..{top+460}")
    # row hitboxes in window px:
    # sidebar folder rows: content y = 52+i*32 .. +28  -> window y = top+52+i*32 .. top+80+i*32
    # Account row: top + 212 .. top + 240
    print("Inbox row  center:", (x0 + 70, top + 52 + 16))
    print("Sent row   center:", (x0 + 70, top + 84 + 16))
    print("Account row center:", (x0 + 70, top + 212 + 16))
    # list first row center: content y = TOOL_H+26+15 = 73 -> window top+73 ; x = x0+150+200
    print("list row0  center:", (x0 + 150 + 200, top + 73))
    # toolbar buttons: content y 5..27 -> window top+5..top+27
    print("Back btn   center:", (x0 + 150 + 6 + 22, top + 16))
    print("Compose btn center:", (x0 + 150 + 6 + 31, top + 16))
    print("Source btn center:", (x0 + 150 + 582 + 29, top + 16))
