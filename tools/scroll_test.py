# -*- coding: utf-8 -*-
# Long-page virtual-scroll check: launch, send PageDown keys, capture.
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
gdi = ctypes.windll.gdi32
EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
pgdns = int(sys.argv[2]) if len(sys.argv) > 2 else 3
out_png = sys.argv[3] if len(sys.argv) > 3 else 'shot_scroll.png'
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'scroll_one.log')

with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
time.sleep(5.0)

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

wins = find('nefuOS 0.1')
if not wins:
    print('NO WINDOW')
    p.terminate(); sys.exit(1)
hwnd = wins[-1]
u32.SetForegroundWindow(hwnd)
time.sleep(0.5)
fg = u32.GetForegroundWindow()
print('fg matches:', fg == hwnd, hex(fg), hex(hwnd))
# click into the page body to make sure the browser (not the address bar) has focus
pt = wintypes.POINT(400, 400)
u32.ClientToScreen(hwnd, ctypes.byref(pt))
u32.SetCursorPos(pt.x, pt.y)
time.sleep(0.2)
u32.mouse_event(0x0002, 0, 0, 0, 0)
time.sleep(0.1)
u32.mouse_event(0x0004, 0, 0, 0, 0)
time.sleep(0.6)

class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]
class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("ki", KEYBDINPUT)]

VK_NEXT = 0x22
KEYEVENTF_KEYUP = 0x0002

def send(vk, up=False):
    inp = INPUT()
    inp.type = 1  # INPUT_KEYBOARD
    inp.ki.wVk = vk
    if up: inp.ki.dwFlags = KEYEVENTF_KEYUP
    u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))

for i in range(pgdns):
    send(VK_NEXT); time.sleep(0.08); send(VK_NEXT, True); time.sleep(0.5)
time.sleep(1.2)

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
print('shot', out_png, 'client', cr.right, cr.bottom)

p.terminate(); time.sleep(0.3)
try:
    txt = open(log, encoding='utf-8', errors='replace').read()
except Exception:
    txt = ''
for key in ('worker parsed', 'E: ', 'fatal'):
    for line in txt.splitlines():
        if key in line:
            print('LOG:', line.strip()[:160])
