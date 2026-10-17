import subprocess, sys, time, os, ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32

# close leftover WER dialogs (Error launching CrashSender.exe etc.)
EnumWindows = user32.EnumWindows; GetWindowTextW = user32.GetWindowTextW; PostMessageW = user32.PostMessageW
def close_wer():
    titles = []
    def cb(h, l):
        b = ctypes.create_unicode_buffer(256); GetWindowTextW(h, b, 256)
        t = b.value
        if 'CrashSender' in t or 'Microsoft Windows' in t or 'Error launching' in t:
            titles.append((h, t))
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    EnumWindows(WNDENUMPROC(cb), 0)
    for h, t in titles:
        PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE
        print("closed WER:", t, flush=True)
    return titles
close_wer()

exe = r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
url = "https://bloxd.io/"
outlog = r"D:\mycppos1\nefuOS\verify_out\bloxd_long_stdout.log"
png = r"D:\mycppos1\nefuOS\verify_out\bloxd_long.png"
timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 45
p = subprocess.Popen([exe, "--app", "17", "--url", url], stdout=open(outlog, "wb"), stderr=subprocess.STDOUT)
pid = p.pid
t0 = time.time(); rc = None
while time.time() - t0 < timeout:
    rc = p.poll()
    if rc is not None: break
    time.sleep(0.5)
time.sleep(1)
# find nefuOS window by PID, screenshot it
hwnd = None
def find_cb(h, l):
    global hwnd
    wpid = wintypes.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(wpid))
    if wpid.value == pid:
        b = ctypes.create_unicode_buffer(256); user32.GetWindowTextW(h, b, 256)
        if b.value and user32.IsWindowVisible(h):
            hwnd = h
    return True
WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(find_cb), 0)
if hwnd:
    GDI32 = ctypes.windll.gdi32
    rect = wintypes.RECT(); user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w, h = rect.right-rect.left, rect.bottom-rect.top
    hdc = user32.GetWindowDC(hwnd)
    mem = GDI32.CreateCompatibleDC(hdc); bmp = GDI32.CreateCompatibleBitmap(hdc, w, h)
    GDI32.SelectObject(mem, bmp); GDI32.BitBlt(mem, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)
    class BI(ctypes.Structure):
        _fields_=[("bV",wintypes.DWORD),("bW",wintypes.LONG),("bH",wintypes.LONG),("bPlanes",wintypes.WORD),("bBitCount",wintypes.WORD),("bComp",wintypes.DWORD),("bSizeImg",wintypes.DWORD),("bXppm",wintypes.LONG),("bYppm",wintypes.LONG),("bClrU",wintypes.DWORD),("bClrI",wintypes.DWORD)]
    bi=BI(); bi.bV=40; bi.bW=w; bi.bH=h; bi.bPlanes=1; bi.bBitCount=24; bi.bComp=0
    sz=((w*3+3)&~3)*h; buf=ctypes.create_string_buffer(sz)
    GDI32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    import struct
    fh=struct.pack("<2sIHHI", b"BM", 14+40+sz, 0,0, 54); ih=struct.pack("<IiiHHIIiiII", 40,w,h,1,24,0,sz,0,0,0,0)
    open(png,"wb").write(fh+ih+buf.raw)
    GDI32.DeleteObject(bmp); GDI32.DeleteDC(mem); user32.ReleaseDC(hwnd, hdc)
    print("window %s %dx%d saved" % (hwnd, w, h), flush=True)
else:
    print("no nefuOS window found (pid %d)" % pid, flush=True)
close_wer()
if rc is not None:
    print("CRASH rc=0x%08X" % (rc & 0xFFFFFFFF), flush=True)
else:
    p.kill(); print("ALIVE_%ds" % int(timeout), flush=True)
