import ctypes, ctypes.wintypes, subprocess, time, os, sys, io
from PIL import Image, ImageGrab

u32 = ctypes.windll.user32
class POINT(ctypes.Structure):
    _fields_ = [("X", ctypes.c_long), ("Y", ctypes.c_long)]

def find_window():
    for _ in range(60):
        time.sleep(0.5)
        h = u32.FindWindowW(None, "nefuOS 0.1 - desktop")
        if h:
            return h
    return None

def grab(hwnd):
    rc = ctypes.wintypes.RECT()
    u32.GetClientRect(hwnd, ctypes.byref(rc))
    pt = POINT(); u32.ClientToScreen(hwnd, ctypes.byref(pt))
    return ImageGrab.grab(bbox=(pt.X, pt.Y, pt.X + rc.right, pt.Y + rc.bottom))

TBAR = (15, 23, 42)
LINK = (37, 99, 235)

def chrome_top(img):
    px = img.load()
    for y in range(20, 560, 2):
        n = 0
        for x in range(40, 700, 2):
            r, g, b = px[x, y]
            if abs(r-15) <= 6 and abs(g-23) <= 6 and abs(b-42) <= 6:
                n += 1
        if n >= 60:
            return y
    return -1

def link_y(img):
    px = img.load()
    ys = []
    for y in range(150, 600, 2):
        for x in range(0, 300, 2):
            r, g, b = px[x, y]
            if abs(r-37) <= 8 and abs(g-99) <= 8 and abs(b-235) <= 8:
                ys.append(y)
    return (min(ys), max(ys)) if ys else None

exe = os.path.join(os.environ["TEMP"], "nefu_fix_test", "nefuOS.exe")
log = os.path.join(os.environ["TEMP"], "nefu_drift.log")
try: os.remove(log)
except OSError: pass
flog = open(log, "w", encoding="utf-8", buffering=1)
proc = subprocess.Popen([exe, "--app", "17", "--url", "http://example.com"], stdout=flog, stderr=subprocess.STDOUT)

def loglines():
    with io.open(log, "r", encoding="utf-8") as f:
        return f.read().splitlines()

# wait for commit
t0 = time.time()
while time.time() - t0 < 90:
    time.sleep(0.5)
    if any("runs=19" in ln for ln in loglines()):
        break
print("committed at t=%.1fs" % (time.time() - t0))
hwnd = find_window()
u32.ShowWindow(hwnd, 9); u32.SetForegroundWindow(hwnd)
time.sleep(1)
for i in range(6):
    img = grab(hwnd)
    ct = chrome_top(img)
    ly = link_y(img)
    print("t=+%d chrome_top=%d link_y=%s" % (i * 5, ct, ly))
    img.save(r"D:\mycppos1\nefuOS\build\bare\drift_%d.png" % i)
    time.sleep(5)
proc.terminate()
print("DRIFT TEST DONE")
