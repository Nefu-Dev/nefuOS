import ctypes, ctypes.wintypes as wt, subprocess, time, os, sys
from PIL import Image

EXE = r"C:\Users\huawei\AppData\Local\Temp\nefu_fix_test\nefuOS.exe"
OUT = r"D:\mycppos1\nefuOS\shots"
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

# --- window helpers ---
def find_hwnd(pid):
    hwnds = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        pid2 = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid2))
        if pid2.value == pid:
            hwnds.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return hwnds[0] if hwnds else None

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER)]

def snap(h, name):
    rect = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(rect))
    w = rect.right - rect.left; ht = rect.bottom - rect.top
    hdc = user32.GetWindowDC(h)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, ht)
    old = gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(h, mdc, 2)
    bmpinfo = BITMAPINFO()
    bmpinfo.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmpinfo.bmiHeader.biWidth = w
    bmpinfo.bmiHeader.biHeight = -ht
    bmpinfo.bmiHeader.biPlanes = 1
    bmpinfo.bmiHeader.biBitCount = 32
    bmpinfo.bmiHeader.biCompression = 0  # BI_RGB
    buf = ctypes.create_string_buffer(w * ht * 4)
    gdi32.GetDIBits(mdc, bmp, 0, ht, buf, ctypes.byref(bmpinfo), 0)
    img = Image.frombuffer("RGBA", (w, ht), buf, "raw", "BGRA", 0, 1).convert("RGB")
    gdi32.SelectObject(mdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(h, hdc)
    img.save(os.path.join(OUT, name if name.lower().endswith(".png") else name + ".png"))
    print("shot:", name)

def post(h, m, wp, lp):
    user32.PostMessageW(h, m, wp, lp)

def key(h, vk, ms=60):
    post(h, 0x100, vk, 0); time.sleep(0.03)
    post(h, 0x101, vk, 0); time.sleep(ms / 1000.0)

def type_text(h, s):
    for c in s:
        if c == ' ':
            key(h, 0x20)
        elif 'a' <= c <= 'z':
            key(h, 0x41 + ord(c) - ord('a'))
        elif '0' <= c <= '9':
            key(h, 0x30 + ord(c) - ord('0'))

def click(h, x, y, ms=0.35):
    lp = (y << 16) | (x & 0xFFFF)
    post(h, 0x200, 0, lp)
    post(h, 0x201, 1, lp)
    time.sleep(0.07)
    post(h, 0x202, 0, lp)
    time.sleep(ms)

def dblclick(h, x, y):
    click(h, x, y, 0.15); click(h, x, y, 0.55)

# --- pixel helpers ---
def content_top(img):
    px = img.load()
    # find sidebar column run of color (44,62,80) right of desktop icons
    col = None
    for x in range(40, 250):
        cnt = sum(1 for y in range(100, 560, 2) if px[x, y] == (44, 62, 80))
        if cnt > 100:
            col = x; break
    if col is None:
        return None, None
    # content top = first pixel of the sidebar background in a column
    top = None
    for y in range(0, 629):
        if px[col + 5, y] == (44, 62, 80):
            top = y
            break
    return col, top

def countdiff(a, b, box=None):
    ia = Image.open(os.path.join(OUT, a)).convert("RGB")
    ib = Image.open(os.path.join(OUT, b)).convert("RGB")
    pa = ia.load(); pb = ib.load()
    x0, y0, x1, y1 = box if box else (0, 0, ia.size[0], ia.size[1])
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pa[x, y] != pb[x, y]:
                n += 1
    return n

# --- main ---
for p in ("nefuOS.exe",):
    subprocess.run(["taskkill", "/F", "/IM", p], capture_output=True)
time.sleep(0.5)
DEVNULL = open(os.devnull, "wb")
proc = subprocess.Popen([EXE], cwd=os.path.dirname(EXE), stdout=DEVNULL, stderr=DEVNULL)
time.sleep(4)
h = find_hwnd(proc.pid)
if not h:
    print("FAIL: no window"); sys.exit(1)
print("hwnd", h)

# setup + unlock
type_text(h, "user"); key(h, 0x09)
type_text(h, "user"); key(h, 0x09)
type_text(h, "user"); key(h, 0x09)
key(h, 0x0D)
time.sleep(0.8)
type_text(h, "user"); key(h, 0x0D)
time.sleep(0.8)

dblclick(h, 364, 108)
time.sleep(0.6)
snap(h, "g0_list_raw")

img = Image.open(os.path.join(OUT, "g0_list_raw.png"))
cx, top = content_top(img)
print("content x0 =", cx, " content top =", top)
if top is None:
    print("FAIL: cannot calibrate"); sys.exit(1)

X = 84  # content left offset
def P(cx2, cy):
    return X + cx2, top + cy

# normalized Inbox
click(h, *P(70, 66))
snap(h, "g1_inbox")

# open already-read msg0 -> read
click(h, *P(350, 73))
snap(h, "g2_read")

# scroll body
for _ in range(3):
    key(h, 0x28, 0.12)
snap(h, "g3_read_scrolled")

# Source toggle (content 582..640, y 5..27) -> plain toggle back
click(h, *P(611, 16))
snap(h, "g4_read_source")
click(h, *P(611, 16))
snap(h, "g5_read_plain")

# Back (content 156..200)
click(h, *P(178, 16))
snap(h, "g6_list_after_read")

# open unread msg1 (marks read)
click(h, *P(350, 103))
snap(h, "g7_read_msg1")
click(h, *P(178, 16))
snap(h, "g8_list_after_msg1")

# Compose (content 156..218)
click(h, *P(187, 16))
snap(h, "g9_compose")

# ESC -> draft saved -> Drafts; back to Inbox
key(h, 0x1B, 0.35)
click(h, *P(70, 66))
snap(h, "g10_list_after_compose")

# Account (content 6..144, 212..240)
click(h, *P(70, 226))
snap(h, "g11_account")

# Account Back (content 224..286)
click(h, *P(255, 16))
snap(h, "g12_list_after_account")

print("== pixel comparisons ==")
print("g1 vs g6 (read->list, msg0 already read):", countdiff("g1_inbox.png", "g6_list_after_read.png"))
print("g8 vs g10 (compose->list, above status):", countdiff("g8_list_after_msg1.png", "g10_list_after_compose.png", box=(0, 0, 806, 600)))
print("g10 vs g12 (account->list):", countdiff("g10_list_after_compose.png", "g12_list_after_account.png"))
print("g2 vs g5 (read plain after source toggle):", countdiff("g2_read.png", "g5_read_plain.png"))
print("DONE")
