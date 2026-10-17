from PIL import Image
im = Image.new("RGB", (80, 60), (200, 80, 40))
im.save(r"D:\mycppos1\nefuOS\_testimg.jpg", "JPEG")
print("ok")
