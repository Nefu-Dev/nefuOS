# -*- coding: utf-8 -*-
# One-shot event test v2: launch -> click via SendInput absolute coords
# (DPI-safe) at CLIENT (cx,cy) -> capture -> report logs.
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
gdi = ctypes.windll.gdi32
EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/evt_test.html'
cx, cy = int(sys.argv[2]), int(sys.argv[3])
out_png = sys.argv[4] if len(sys.argv) > 4 else 'shot_evt.png'
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'evt_one.log')

with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
time.sleep(6.0)

def find(sub):
    out = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if u32.IsWindowVisible(h):
            n = u32.GetWindowTextLengthW(h)
            if n > 0:
                b = ctypes.create_unicode_buffer(n + 1)
                u32.GetWindowTextW(h, b, n + 1)
                if sub.lower() in b.value.lower():
                    out.append(h)
        return True
    u32.EnumWindows(cb, 0)
    return out

wins = find('nefuOS')
if not wins:
    print('NO WINDOW')
    p.terminate()
    sys.exit(1)
hwnd = wins[-1]
u32.SetForegroundWindow(hwnd)
time.sleep(0.4)

# client point -> screen (logical) -> SendInput absolute (physical, DPI-safe)
pt = wintypes.POINT(cx, cy)
u32.ClientToScreen(hwnd, ctypes.byref(pt))
sw = u32.GetSystemMetrics(0)
sh = u32.GetSystemMetrics(1)
norm_x = int(pt.x * 65535 // sw)
norm_y = int(pt.y * 65535 // sh)
print('client', cx, cy, 'screen', pt.x, pt.y, 'norm', norm_x, norm_y)

class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]
class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("mi", MOUSEINPUT)]
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_ABSOLUTE = 0x8000
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004

def send(flags):
    inp = INPUT()
    inp.type = 0  # INPUT_MOUSE
    inp.mi.dx = norm_x
    inp.mi.dy = norm_y
    inp.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | flags
    inp.mi.dwExtraInfo = ctypes.pointer(ctypes.c_ulong(0))
    u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))

send(MOUSEEVENTF_LEFTDOWN)
time.sleep(0.15)
send(MOUSEEVENTF_LEFTUP)
time.sleep(1.3)

cr = wintypes.RECT(); u32.GetClientRect(hwnd, ctypes.byref(cr))
hdcw = u32.GetWindowDC(hwnd)
mdc = gdi.CreateCompatibleDC(hdcw)
bmp = gdi.CreateCompatibleBitmap(hdcw, cr.right, cr.bottom)
gdi.SelectObject(mdc, bmp)
u32.PrintWindow(hwnd, mdc, 2)
class BIH(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]
bi = BIH(); bi.biSize = ctypes.sizeof(BIH)
bi.biWidth = cr.right; bi.biHeight = -cr.bottom; bi.biPlanes = 1; bi.biBitCount = 32
buf = ctypes.create_string_buffer(cr.right * cr.bottom * 4)
gdi.GetDIBits(mdc, bmp, 0, cr.bottom, buf, ctypes.byref(bi), 0)
gdi.DeleteObject(bmp); gdi.DeleteDC(mdc); u32.ReleaseDC(hwnd, hdcw)
from PIL import Image
Image.frombuffer("RGB", (cr.right, cr.bottom), buf.raw, "raw", "BGRX", 0, 1).save(out_png)
print('shot', out_png)

p.terminate()
time.sleep(0.3)
try:
    txt = open(log, encoding='utf-8', errors='replace').read()
except Exception:
    txt = ''
for key in ('worker parsed', 'js dom re-render', 'via click', 'E: '):
    for line in txt.splitlines():
        if key in line:
            print('LOG:', line.strip())
