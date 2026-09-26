from PIL import Image
im = Image.open(r'D:\mycppos1\nefuOS\tools\bgtests\shot_0.bmp').convert('RGB')
im.crop((159, 182, 159+800, 182+600)).resize((400, 300)).save(r'D:\mycppos1\nefuOS\tools\bgtests\shot_0_view.png')
print('ok')
