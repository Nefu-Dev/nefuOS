import ctypes, ctypes.wintypes, subprocess, time, os, sys, threading, io
from PIL import Image, ImageGrab

u32 = ctypes.windll.user32

class POINT(ctypes.Structure):
    _fields_ = [("X", ctypes.c_long), ("Y", ctypes.c_long)]
class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_int), ("dy", ctypes.c_int), ("mouseData", ctypes.c_uint),
                ("dwFlags", ctypes.c_uint), ("time", ctypes.c_uint), ("extra", ctypes.POINTER(ctypes.c_ulong))]
class INPUT(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint), ("mi", MOUSEINPUT)]

def click_abs(x, y, sw, sh):
    def inp(flags):
        i = INPUT(); i.type = 0
        i.mi.dx = int(x * 65535.0 / sw); i.mi.dy = int(y * 65535.0 / sh)
        i.mi.dwFlags = flags
        return i
    u32.SendInput(1, ctypes.byref(inp(0x0001 | 0x8000)), ctypes.sizeof(INPUT))
    time.sleep(0.06)
    u32.SendInput(1, ctypes.byref(inp(0x0002 | 0x8000)), ctypes.sizeof(INPUT))
    time.sleep(0.06)
    u32.SendInput(1, ctypes.byref(inp(0x0004 | 0x8000)), ctypes.sizeof(INPUT))

def find_window():
    for _ in range(60):
        time.sleep(0.5)
        h = u32.FindWindowW(None, "nefuOS 0.1 - desktop")
        if h:
            return h
    return None

def client_origin(hwnd):
    rc = ctypes.wintypes.RECT()
    u32.GetClientRect(hwnd, ctypes.byref(rc))
    pt = POINT(); u32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.X, pt.Y, rc.right, rc.bottom

def grab(hwnd, ox, oy, cw, ch):
    return ImageGrab.grab(bbox=(ox, oy, ox + cw, oy + ch))

def count_px(img, mode, xmin, xmax, ymin, ymax):
    # mode: 'blue' = bluish tint (link text; capture shifts colors), 'gray' = gray band (scrollbar)
    px = img.load()
    n = 0
    for y in range(ymin, min(ymax, img.height)):
        for x in range(xmin, min(xmax, img.width)):
            try: c = px[x, y]
            except Exception: continue
            r, g, b = (c[0], c[1], c[2]) if len(c) >= 3 else (c, c, c)
            if mode == 'blue':
                if b > 180 and b > r + 40 and b > g + 30:
                    n += 1
            elif mode == 'gray':
                if abs(r - g) < 12 and abs(g - b) < 12 and 128 < r < 245:
                    n += 1
    return n

exe = os.path.join(os.environ["TEMP"], "nefu_fix_test", "nefuOS.exe")
out = r"D:\mycppos1\nefuOS\build\bare"
logfile = os.path.join(os.environ["TEMP"], "nefu_nav_run.log")
try: os.remove(logfile)
except OSError: pass
flog = open(logfile, "w", encoding="utf-8", buffering=1)
proc = subprocess.Popen([exe, "--app", "17", "--url", "http://example.com"],
                        stdout=flog, stderr=subprocess.STDOUT)

hwnd = None
for _ in range(120):
    time.sleep(0.5)
    hwnd = u32.FindWindowW(None, "nefuOS 0.1 - desktop")
    if hwnd: break
if not hwnd:
    print("NO WINDOW"); proc.terminate(); sys.exit(1)
u32.ShowWindow(hwnd, 9); u32.SetForegroundWindow(hwnd)
time.sleep(1)
ox, oy, cw, ch = client_origin(hwnd)
print("client origin:", ox, oy, cw, ch)
SW, SH = 1536, 960

def example_committed(img):
    # example.com link blue pixels near fb x 0-150 (chrome-relative, capture may
    # shift vertically; scan a generous y band)
    return count_px(img, 'blue', 0, 150, 200, 560) > 40

def iana_committed(img):
    # WM scrollbar thumb vertical strip at fb x ~772 (capture x jitter tolerated)
    return count_px(img, 'gray', 700, 800, 200, 560) > 200

# 1) wait for example.com commit
t0 = time.time()
ok = False
while time.time() - t0 < 90:
    time.sleep(1.0)
    img = grab(hwnd, ox, oy, cw, ch)
    if example_committed(img):
        ok = True; break
    if time.time() - t0 > 30:
        img.save(out + r"\nav_0_before.png")
if not ok:
    print("TIMEOUT waiting example.com commit"); proc.terminate(); sys.exit(1)
time.sleep(1.0)
img = grab(hwnd, ox, oy, cw, ch)
img.save(out + r"\nav_0_before.png")
print("example.com committed")

# 2) click believed link: fb (60, 295) -> screen
sx, sy = ox + 60, oy + 295
print("click link at", sx, sy)
click_abs(sx, sy, SW, SH)

t0 = time.time(); ok = False
while time.time() - t0 < 45:
    time.sleep(0.8)
    img = grab(hwnd, ox, oy, cw, ch)
    if iana_committed(img):
        ok = True; break
if not ok:
    img.save(out + r"\nav_1_fail.png")
    print("TIMEOUT waiting iana (link click)"); proc.terminate(); sys.exit(1)
time.sleep(1.5)
img = grab(hwnd, ox, oy, cw, ch)
img.save(out + r"\nav_1_after_link.png")
print("NAVIGATED via link click (iana.org, scrollbar visible)")

# 3) back button: fb (36,118)
click_abs(ox + 36, oy + 118, SW, SH)
t0 = time.time(); ok = False
while time.time() - t0 < 40:
    time.sleep(0.8)
    img = grab(hwnd, ox, oy, cw, ch)
    if example_committed(img) and not iana_committed(img):
        ok = True; break
if not ok:
    print("TIMEOUT waiting back"); proc.terminate(); sys.exit(1)
time.sleep(1.0)
img = grab(hwnd, ox, oy, cw, ch)
img.save(out + r"\nav_2_after_back.png")
print("BACK OK (example.com restored)")

# 4) forward button: fb (64,118)
click_abs(ox + 64, oy + 118, SW, SH)
t0 = time.time(); ok = False
while time.time() - t0 < 40:
    time.sleep(0.8)
    img = grab(hwnd, ox, oy, cw, ch)
    if iana_committed(img):
        ok = True; break
if not ok:
    print("TIMEOUT waiting fwd"); proc.terminate(); sys.exit(1)
time.sleep(1.0)
img = grab(hwnd, ox, oy, cw, ch)
img.save(out + r"\nav_3_after_fwd.png")
print("FWD OK (iana restored)")
proc.terminate()
print("NAV TEST PASSED")
