from PIL import Image

a = Image.open(r'D:\mycppos1\nefuOS\verify_out\bloxd_menu_worker.png').convert('RGB')
b = Image.open(r'D:\mycppos1\nefuOS\build\bare\cache_bloxd.png').convert('RGB')
print('sizes', a.size, b.size)

pa = a.load(); pb = b.load()

def white_count(im, p):
    n = 0
    w, h = im.size
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, bl = p[x, y]
            if r + g + bl > 600:
                n += 1
    return n

print('worker white px (sampled)', white_count(a, pa))
print('main   white px (sampled)', white_count(b, pb))
print('worker px[100,100]', pa[100,100])
print('main   px[100,100]', pb[100,100])
print('worker px[400,300]', pa[400,300])
print('main   px[400,300]', pb[400,300])

# flips
b.transpose(Image.FLIP_LEFT_RIGHT).save(r'D:\mycppos1\nefuOS\build\bare\cache_bloxd_flip.png')
b.transpose(Image.FLIP_TOP_BOTTOM).save(r'D:\mycppos1\nefuOS\build\bare\cache_bloxd_vflip.png')
print('flips saved')
