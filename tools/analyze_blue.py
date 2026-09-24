from PIL import Image
img = Image.open(r'D:\mycppos1\nefuOS\build\bare\nav_fail_example.com_load.png').convert('RGB')
px = img.load()
pts = []
for y in range(img.height):
    for x in range(img.width):
        r, g, b = px[x, y]
        if abs(r - 37) <= 10 and abs(g - 99) <= 10 and abs(b - 235) <= 10:
            pts.append((x, y))
print('total link-colored px:', len(pts))
if pts:
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    print('x range', min(xs), max(xs), 'y range', min(ys), max(ys))
    bands = {}
    for p in pts:
        bands.setdefault(p[1] // 10, []).append(p)
    for k in sorted(bands):
        b = bands[k]
        xs2 = [p[0] for p in b]
        print('yband', k * 10, 'count', len(b), 'x', min(xs2), max(xs2))
