// nefuOS Win32
// compile：g++ ... backends/win32/win32.cpp -lgdi32 -luser32
#include <windows.h>
#include <winsock2.h>
#include <wininet.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <icmpapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"
#include "../../core/gui/gfx.h"   // full Surface definition

using namespace nefu;

static const int SW = 800, SH = 600;
static const char* PERSIST_FILE = "nefuos.fs";

static HWND s_hwnd = 0;
static HBITMAP s_dib = 0;
static void* s_bits = 0;
static uint32_t s_heap_used = 0;
static uint8_t s_mouse_buttons = 0;

// ---------------- platform implementation ----------------
namespace nefu {

// ---- GDI+ image decode (host): real jpg/png/gif/bmp decoding ----
static ULONG_PTR s_gp_token = 0;
static bool s_gp_init = false;

bool platform_decode_image(const uint8_t* data, uint32_t size, Surface& out) {
    out.addr = 0;
    if (!data || size < 4) return false;
    if (!s_gp_init) {
        Gdiplus::GdiplusStartupInput in;
        if (Gdiplus::GdiplusStartup(&s_gp_token, &in, 0) != Gdiplus::Ok) return false;
        s_gp_init = true;
    }
    IStream* stm = 0;
    if (CreateStreamOnHGlobal(0, TRUE, &stm) != S_OK) return false;
    ULONG written = 0;
    stm->Write(data, size, &written);
    LARGE_INTEGER zero; zero.QuadPart = 0;
    stm->Seek(zero, STREAM_SEEK_SET, 0);
    Gdiplus::Bitmap bmp(stm, FALSE);
    stm->Release();
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
    Gdiplus::Rect r(0, 0, w, h);
    Gdiplus::BitmapData bd;
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    out.addr = (uint8_t*)kalloc((size_t)w * (size_t)h * 4);
    if (!out.addr) { bmp.UnlockBits(&bd); return false; }
    out.width = w; out.height = h; out.pitch = w * 4;
    const uint8_t* src = (const uint8_t*)bd.Scan0;
    for (int y = 0; y < h; y++) {
        const uint8_t* row = src + (size_t)y * bd.Stride;
        for (int x = 0; x < w; x++) {
            uint32_t b = row[x * 4], g = row[x * 4 + 1], rr = row[x * 4 + 2], a = row[x * 4 + 3];
            if (a < 255) { // alpha blend over white
                rr = (uint32_t)(((int)rr * (int)a + 255 * (255 - (int)a)) / 255);
                g  = (uint32_t)(((int)g  * (int)a + 255 * (255 - (int)a)) / 255);
                b  = (uint32_t)(((int)b  * (int)a + 255 * (255 - (int)a)) / 255);
            }
            out.px(x, y) = (rr << 16) | (g << 8) | b;
        }
    }
    bmp.UnlockBits(&bd);
    return true;
}


void* kalloc(size_t sz) {
    uint32_t* h = (uint32_t*)malloc(sz + 8);
    if (!h) return 0;
    h[0] = (uint32_t)sz;
    s_heap_used += (uint32_t)sz;
    return h + 2;
}

void kfree(void* p) {
    if (!p) return;
    uint32_t* h = (uint32_t*)p - 2;
    s_heap_used -= h[0];
    free(h);
}

Screen* platform_screen() {
    static Screen sc;
    sc.addr = (uint8_t*)s_bits;
    sc.width = SW;
    sc.height = SH;
    sc.pitch = SW * 4;
    return &sc;
}

void platform_present() {
    HDC wdc = GetDC(s_hwnd);
    if (wdc && s_dib) {
        HDC mdc = CreateCompatibleDC(wdc);
        HGDIOBJ old = SelectObject(mdc, s_dib);
        BitBlt(wdc, 0, 0, SW, SH, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteDC(mdc);
    }
    ReleaseDC(s_hwnd, wdc);
}

uint32_t platform_tick_ms() {
    return (uint32_t)(GetTickCount64() & 0xFFFFFFFFu);
}

uint32_t platform_seconds_of_day() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return (uint32_t)st.wHour * 3600u + (uint32_t)st.wMinute * 60u + (uint32_t)st.wSecond;
}

void platform_dbg(const char* s) {
    OutputDebugStringA(s);
    fputs(s, stdout);
    fflush(stdout);
}

bool platform_fs_load(uint8_t** out, uint32_t* out_size) {
    FILE* f = fopen(PERSIST_FILE, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)kalloc((size_t)n);
    if (!buf) { fclose(f); return false; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (r != (size_t)n) { kfree(buf); return false; }
    *out = buf;
    *out_size = (uint32_t)n;
    return true;
}

void platform_fs_save(const uint8_t* data, uint32_t size) {
    FILE* f = fopen(PERSIST_FILE, "wb");
    if (!f) return;
    fwrite(data, 1, size, f);
    fclose(f);
}

void platform_poweroff() {
    if (s_hwnd) PostMessageA(s_hwnd, WM_CLOSE, 0, 0);
}

void platform_mem_stats(uint32_t* used, uint32_t* total) {
    *used = s_heap_used;
    *total = 64u * 1024u * 1024u;
}

const char* platform_name() { return "win32"; }

// ---- real network adapter info (host): GetAdaptersInfo ----
bool platform_net_get(NetAdapterInfo* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    ULONG sz = 0;
    GetAdaptersInfo(0, &sz);
    if (sz == 0) return false;
    IP_ADAPTER_INFO* ai = (IP_ADAPTER_INFO*)malloc(sz);
    if (!ai) return false;
    bool ok = false;
    if (GetAdaptersInfo(ai, &sz) == NO_ERROR) {
        // pass 1: prefer an adapter that actually has a gateway (the real
        // uplink), pass 2: fall back to any adapter with a non-zero IP
        for (int pass = 0; pass < 2 && !ok; pass++) {
            for (IP_ADAPTER_INFO* p = ai; p; p = p->Next) {
                if (p->Type == MIB_IF_TYPE_LOOPBACK) continue;
                if (p->IpAddressList.IpAddress.String[0] == '0' ||
                    p->IpAddressList.IpAddress.String[0] == 0) continue;
                if (pass == 0 && p->GatewayList.IpAddress.String[0] == '0') continue;
                memcpy(out->mac, p->Address, (p->AddressLength > 6) ? 6 : p->AddressLength);
                out->ip = inet_addr(p->IpAddressList.IpAddress.String);
                out->gw = inet_addr(p->GatewayList.IpAddress.String);
                out->mask = inet_addr(p->IpAddressList.IpMask.String);
                out->up = (p->Type != MIB_IF_TYPE_OTHER);
                strncpy(out->name, p->Description, 47);
                out->name[47] = 0;
                ok = true;
                break;
            }
        }
    }
    free(ai);
    return ok;
}

// ---- real Wi-Fi scan (host): WlanEnumInterfaces + WlanGetAvailableNetworkList ----
int platform_wifi_scan(WifiNetInfo* list, int max) {
    if (!list || max <= 0) return 0;
    HANDLE h = 0;
    DWORD ver = 0;
    if (WlanOpenHandle(2, 0, &ver, &h) != ERROR_SUCCESS) return 0;
    WLAN_INTERFACE_INFO_LIST* il = 0;
    int n = 0;
    if (WlanEnumInterfaces(h, 0, &il) == ERROR_SUCCESS && il) {
        for (ULONG i = 0; i < il->dwNumberOfItems && n < max; i++) {
            WLAN_AVAILABLE_NETWORK_LIST* al = 0;
            if (WlanGetAvailableNetworkList(h, &il->InterfaceInfo[i].InterfaceGuid, 0, 0, &al) == ERROR_SUCCESS && al) {
                for (ULONG j = 0; j < al->dwNumberOfItems && n < max; j++) {
                    const WLAN_AVAILABLE_NETWORK& a = al->Network[j];
                    const DOT11_SSID& ss = a.dot11Ssid;
                    if (ss.uSSIDLength == 0 || ss.uSSIDLength > 32) continue;
                    char tmp[33];
                    memcpy(tmp, ss.ucSSID, ss.uSSIDLength);
                    tmp[ss.uSSIDLength] = 0;
                    bool dup = false;
                    for (int k = 0; k < n; k++)
                        if (strcmp(list[k].ssid, tmp) == 0) { dup = true; break; }
                    if (dup) continue;
                    strncpy(list[n].ssid, tmp, 32);
                    list[n].ssid[32] = 0;
                    list[n].rssi = (int)a.wlanSignalQuality;      // 0..100
                    list[n].open = (a.bSecurityEnabled == FALSE);
                    n++;
                }
                WlanFreeMemory(al);
            }
        }
        WlanFreeMemory(il);
    }
    WlanCloseHandle(h, 0);
    return n;
}

// ---- real ICMP ping (host): IcmpSendEcho ----
bool platform_ping(uint32_t ip, int timeout_ms) {
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return false;
    struct { ICMP_ECHO_REPLY rep; uint8_t pad[8]; } buf;
    memset(&buf, 0, sizeof(buf));
    DWORD r = IcmpSendEcho(h, ip, 0, 0, 0, &buf.rep, (DWORD)sizeof(buf), (DWORD)timeout_ms);
    IcmpCloseHandle(h);
    return r != 0 && buf.rep.Status == IP_SUCCESS;
}

// ---- real HTTP(S) GET (host): WinINet with DNS + TLS ----
bool platform_http_get(const char* url, uint8_t** out, uint32_t* out_size) {
    if (!out || !out_size) return false;
    *out = 0; *out_size = 0;
    if (!url) return false;
    HINTERNET h = InternetOpenA("nefuOS/0.2", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (!h) return false;
    HINTERNET u = InternetOpenUrlA(h, url, 0, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!u) { InternetCloseHandle(h); return false; }
    uint32_t cap = 65536, len = 0;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) { InternetCloseHandle(u); InternetCloseHandle(h); return false; }
    char tmp[4096];
    DWORD rd = 0;
    for (;;) {
        if (!InternetReadFile(u, tmp, sizeof(tmp), &rd) || rd == 0) break;
        if (len + rd > cap) {
            cap *= 2;
            uint8_t* nb = (uint8_t*)realloc(buf, cap);
            if (!nb) { free(buf); buf = 0; break; }
            buf = nb;
        }
        memcpy(buf + len, tmp, rd);
        len += rd;
    }
    InternetCloseHandle(u);
    InternetCloseHandle(h);
    if (!buf || len == 0) { if (buf) free(buf); return false; }
    *out = buf;
    *out_size = len;
    return true;
}

} // namespace nefu

// ---------------- ----------------
static int translate_key(int vk) {
    switch (vk) {
    case VK_RETURN: return KEY_ENTER;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_ESCAPE: return KEY_ESC;
    case VK_LEFT: return KEY_LEFT;
    case VK_UP: return KEY_UP;
    case VK_RIGHT: return KEY_RIGHT;
    case VK_DOWN: return KEY_DOWN;
    case VK_TAB: return KEY_TAB;
    case VK_DELETE: return KEY_DEL;
    case VK_HOME: return KEY_HOME;
    case VK_END: return KEY_END;
    case VK_PRIOR: return KEY_PGUP;
    case VK_NEXT: return KEY_PGDN;
    case VK_SPACE: return KEY_SPACE;
    case VK_F1: return KEY_F1;
    case VK_F2: return KEY_F2;
    case VK_F3: return KEY_F3;
    case VK_F4: return KEY_F4;
    case VK_F5: return KEY_F5;
    case VK_F6: return KEY_F6;
    case VK_F7: return KEY_F7;
    case VK_F8: return KEY_F8;
    case VK_F9: return KEY_F9;
    case VK_F10: return KEY_F10;
    case VK_F11: return KEY_F11;
    case VK_F12: return KEY_F12;
    case VK_SHIFT: return KEY_SHIFT;
    case VK_CONTROL: return KEY_CTRL;
    case VK_MENU: return KEY_ALT;
    case VK_CAPITAL: return KEY_CAPS;
    default: return KEY_NONE;
    }
}

static char translate_ascii(int vk) {
    UINT ch = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_CHAR);
    char c = (ch & 0x80000000) ? 0 : (char)(ch & 0xFF);
    // none Shift/Caps （MapVirtualKey ）
    if (c >= 'A' && c <= 'Z') {
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool caps = (GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0;
        if (shift != caps) return c;
        return (char)(c + 32);
    }
    return c;
}

// ---------------- ----------------
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        nefuos_tick();
        nefuos_frame();
        return 0;
    }
    case WM_KEYDOWN: {
        int kc = translate_key((int)wp);
        char ac = translate_ascii((int)wp);
        if (kc == KEY_SPACE) ac = ' ';
        nefuos_handle_key(kc, ac, true);
        return 0;
    }
    case WM_KEYUP: {
        nefuos_handle_key(translate_key((int)wp), 0, false);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = (int)(short)LOWORD(lp);
        int y = (int)(short)HIWORD(lp);
        nefuos_handle_mouse(x, y, s_mouse_buttons);
        return 0;
    }
    case WM_LBUTTONDOWN: s_mouse_buttons |= 0x01; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_LBUTTONUP: s_mouse_buttons &= (uint8_t)~0x01; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_RBUTTONDOWN: s_mouse_buttons |= 0x02; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_RBUTTONUP: s_mouse_buttons &= (uint8_t)~0x02; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_MBUTTONDOWN: s_mouse_buttons |= 0x04; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_MBUTTONUP: s_mouse_buttons &= (uint8_t)~0x04; nefuos_handle_mouse((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), s_mouse_buttons); return 0;
    case WM_MOUSEWHEEL: {
        short d = (short)HIWORD(wp);
        nefuos_handle_scroll(d > 0 ? 120 : -120);
        return 0;
    }
    case WM_CLOSE:
        nefuos_shutdown();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        nefuos_frame();
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

// ---------------- （） ----------------
#include <windows.h>
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    FILE* f = fopen("crash.txt", "a");
    if (f) {
        fprintf(f, "CRASH code=0x%lX addr=0x%p\n",
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------------- entry ----------------
int main() {
    SetUnhandledExceptionFilter(crash_handler);
    printf("nefuOS host backend (win32) starting...\n");

    HINSTANCE hInst = GetModuleHandleA(0);

    // create 800x600 32bpp DIB
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = SW;
    bi.bmiHeader.biHeight = -SH;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(0);
    s_dib = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &s_bits, 0, 0);
    ReleaseDC(0, sdc);
    if (!s_dib || !s_bits) {
        printf("FATAL: cannot create DIB framebuffer\n");
        return 1;
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "nefuOS";
    if (!RegisterClassA(&wc)) {
        printf("FATAL: cannot register window class\n");
        return 1;
    }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, SW, SH };
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA("nefuOS", "nefuOS 0.1 - desktop", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           0, 0, hInst, 0);
    if (!s_hwnd) {
        printf("FATAL: cannot create window\n");
        return 1;
    }

    nefuos_init();
    ShowWindow(s_hwnd, SW_SHOW);
    SetTimer(s_hwnd, 1, 10, 0);

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    KillTimer(s_hwnd, 1);
    printf("nefuOS host exit\n");
    return 0;
}
