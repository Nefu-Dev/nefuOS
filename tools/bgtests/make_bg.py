from PIL import Image
im = Image.new('RGB', (64, 64), (200, 30, 30))   # solid red bg
im.save(r'D:\mycppos1\nefuOS\tools\bgtests\bg.png')
print('bg.png ok')
