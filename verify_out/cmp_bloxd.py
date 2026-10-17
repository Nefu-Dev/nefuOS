from PIL import Image

a = Image.open(r'D:\mycppos1\nefuOS\verify_out\bloxd_menu_worker.png').convert('RGB')
b = Image.open(r'D:\mycppos1\nefuOS\build\bare\cache_bloxd.png').convert('RGB')
pa = a.load(); pb = b.load()
diff = 0
maxd = 0
for y in range(600):
    for x in range(800):
        ca = pa[x, y]; cb = pb[x, y]
        d = abs(ca[0]-cb[0]) + abs(ca[1]-cb[1]) + abs(ca[2]-cb[2])
        if d:
            diff += 1
            maxd = max(maxd, d)
print('diff pixels', diff, 'of', 800*600, 'max delta', maxd)
