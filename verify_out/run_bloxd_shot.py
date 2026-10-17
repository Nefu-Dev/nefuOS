import subprocess, sys, time, os
exe = r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
url = "https://bloxd.io/"
outlog = r"D:\mycppos1\nefuOS\verify_out\bloxd_guard_stdout.log"
png = r"D:\mycppos1\nefuOS\verify_out\bloxd_guard.png"
timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 25
p = subprocess.Popen([exe, "--app", "17", "--url", url], stdout=open(outlog, "wb"), stderr=subprocess.STDOUT)
t0 = time.time(); rc = None
while time.time() - t0 < timeout:
    rc = p.poll()
    if rc is not None: break
    time.sleep(0.5)
# screenshot regardless
time.sleep(1)
import ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32
GDI32 = ctypes.windll.gdi32
hwnd = user32.GetForegroundWindow()
rect = wintypes.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(rect))
w, h = rect.right-rect.left, rect.bottom-rect.top
hdc = user32.GetWindowDC(hwnd)
mem = GDI32.CreateCompatibleDC(hdc); bmp = GDI32.CreateCompatibleBitmap(hdc, w, h)
GDI32.SelectObject(mem, bmp)
GDI32.BitBlt(mem, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)
class BI(ctypes.Structure): _fields_=[("bV",wintypes.DWORD),("bW",wintypes.LONG),("bH",wintypes.LONG),("bPlanes",wintypes.WORD),("bBitCount",wintypes.WORD),("bComp",wintypes.DWORD),("bSizeImg",wintypes.DWORD),("bXppm",wintypes.LONG),("bYppm",wintypes.LONG),("bClrU",wintypes.DWORD),("bClrI",wintypes.DWORD)]
bi=BI(); bi.bV=40; bi.bW=w; bi.bH=h; bi.bPlanes=1; bi.bBitCount=24; bi.bComp=0
sz = ((w*3+3)&~3)*h
buf = ctypes.create_string_buffer(sz)
GDI32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
# write BMP manually
import struct
fh=struct.pack("<2sIHHI", b"BM", 14+40+sz, 0,0, 54)
ih=struct.pack("<IiiHHIIiiII", 40,w,h,1,24,0,sz,0,0,0,0)
open(png,"wb").write(fh+ih+buf.raw)
GDI32.DeleteObject(bmp); GDI32.DeleteDC(mem); user32.ReleaseDC(hwnd, hdc)
if rc is not None:
    print("CRASH rc=0x%08X" % (rc & 0xFFFFFFFF)); print("CRASH_0xC0000374" if (rc&0xFFFFFFFF)==0xC0000374 else "OTHER")
else:
    p.kill(); print("ALIVE_%ds screenshot saved" % int(timeout))
