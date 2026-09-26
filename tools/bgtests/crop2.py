from PIL import Image
im = Image.open(r'D:\mycppos1\nefuOS\tools\bgtests\pw_0.bmp').convert('RGB')
im.resize((400, 300)).save(r'D:\mycppos1\nefuOS\tools\bgtests\pw_0_view.png')
print('ok')
