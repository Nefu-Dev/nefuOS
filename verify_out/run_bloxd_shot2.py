import subprocess, sys, time, os, ctypes
from ctypes import wintypes
exe = r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
url = "https://bloxd.io/"
outlog = r"D:\mycppos1\nefuOS\verify_out\bloxd_guard_stdout.log"
png = r"D:\mycppos1\nefuOS\verify_out\bloxd_guard.png"
timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 40
p = subprocess.Popen([exe, "--app", "17", "--url", url], stdout=open(outlog, "wb"), stderr=subprocess.STDOUT)
t0 = time.time(); rc = None
user32 = ctypes.windll.user32
GDI32 = ctypes.windll.gdi32
def find_hwnd(pid):
    found = []
    EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(h):
            found.append(h)
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found[0] if found else None
while time.time() - t0 < timeout:
    rc = p.poll()
    if rc is not None: break
    time.sleep(0.5)
time.sleep(1)
hwnd = find_hwnd(p.pid) if rc is None else None
if not hwnd:
    hwnd = user32.GetForegroundWindow()
    print("fallback foreground hwnd")
else:
    print("browser hwnd found, pid=%d" % p.pid)
# optionally bring to front so it renders
try:
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
except Exception as e:
    print("showwnd fail", e)
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
import struct
fh=struct.pack("<2sIHHI", b"BM", 14+40+sz, 0,0, 54)
ih=struct.pack("<IiiHHIIiiII", 40,w,h,1,24,0,sz,0,0,0,0)
open(png,"wb").write(fh+ih+buf.raw)
GDI32.DeleteObject(bmp); GDI32.DeleteDC(mem); user32.ReleaseDC(hwnd, hdc)
print("shot %dx%d hwnd=%s" % (w,h,hwnd))
if rc is not None:
    print("CRASH rc=0x%08X" % (rc & 0xFFFFFFFF))
else:
    p.kill(); print("ALIVE_%ds screenshot saved" % int(timeout))
