# PrintWindow capture of a window by title substring (flags=2: client area only).
import ctypes, sys, time
from ctypes import wintypes

u32 = ctypes.windll.user32
gdi = ctypes.windll.gdi32
k32 = ctypes.windll.kernel32

PW_CLIENTONLY = 2
CF_BITMAP = 2

def find_windows(substr):
    out = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, lparam):
        if u32.IsWindowVisible(hwnd):
            n = u32.GetWindowTextLengthW(hwnd)
            if n > 0:
                buf = ctypes.create_unicode_buffer(n + 1)
                u32.GetWindowTextW(hwnd, buf, n + 1)
                if substr.lower() in buf.value.lower():
                    out.append((hwnd, buf.value))
        return True
    u32.EnumWindows(cb, 0)
    return out

def capture(hwnd, path):
    u32.SetForegroundWindow(hwnd)
    time.sleep(0.4)
    r = wintypes.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdcw = u32.GetWindowDC(hwnd)
    mdc = gdi.CreateCompatibleDC(hdcw)
    bmp = gdi.CreateCompatibleBitmap(hdcw, w, h)
    gdi.SelectObject(mdc, bmp)
    ok = u32.PrintWindow(hwnd, mdc, PW_CLIENTONLY)
    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                    ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                    ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
                    ("biClrImportant", wintypes.DWORD)]
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    got = gdi.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi.DeleteObject(bmp); gdi.DeleteDC(mdc); u32.ReleaseDC(hwnd, hdcw)
    if not got:
        return "PrintWindow failed"
    from PIL import Image
    im = Image.frombuffer("RGB", (w, h), buf.raw, "raw", "BGRX", 0, 1)
    im.save(path)
    return f"ok {w}x{h} pw={ok}"

if __name__ == "__main__":
    sub = sys.argv[1] if len(sys.argv) > 1 else "nefuOS"
    out = sys.argv[2] if len(sys.argv) > 2 else "shot.png"
    wins = find_windows(sub)
    if not wins:
        print("no window found")
        sys.exit(1)
    hwnd, title = wins[-1]  # last = most recently created
    print("target:", hwnd, title)
    print(capture(hwnd, out))