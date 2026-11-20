import glob, os
from PIL import Image, ImageDraw, ImageFont
files = sorted(glob.glob('verify_out/sw3_*.bmp'), key=lambda p:int(os.path.basename(p).split('_')[1]))
names = [os.path.basename(f).replace('sw3_','').replace('.bmp','') for f in files]
COLS=4; TW=340; TH=255
rows=(len(files)+COLS-1)//COLS
sheet=Image.new('RGB',(COLS*TW, rows*(TH+20)),(24,24,28))
d=ImageDraw.Draw(sheet)
try: font=ImageFont.truetype("C:/Windows/Fonts/consola.ttf",13)
except: font=ImageFont.load_default()
for i,f in enumerate(files):
    im=Image.open(f).convert('RGB')
    im=im.resize((TW,TH), Image.LANCZOS)
    x=(i%COLS)*TW; y=(i//COLS)*(TH+20)
    sheet.paste(im,(x,y+20))
    d.text((x+4,y+3), f"{i+1}. {names[i]}", fill=(255,220,120), font=font)
sheet.save('verify_out/sweep3_montage.png')
print('ok', sheet.size, len(files))
