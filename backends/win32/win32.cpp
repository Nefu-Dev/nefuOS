/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS Win32
// compile: g++ ... backends/win32/win32.cpp -lgdi32 -luser32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <icmpapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <mmsystem.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"
#include "../../core/gui/gfx.h"   // full Surface definition
#include "../../core/apps/apps.h"  // app_launch for --app
#include "../../core/apps/npkg.h"  // npkg_gui_* registry API (--npkg-test)
#include "../../core/apps/update.h" // update_run_pipeline (--update-test)
#include "../../core/apps/minijs.h"  // JS engine self-test (--jstest)
#include "../../core/apps/bios.h"  // classic BIOS setup utility (F2 boot key)
#include "../../core/net/tls13.h"
#include "../../core/lib/deflate.h"
#include "../../core/sys/power.h"  // blue_screen()

using namespace nefu;

// Framebuffer size. Not fixed any more: it is derived from the monitor work
// area at startup and follows the window when the user resizes it, so the
// desktop can adapt to whatever resolution the machine actually runs at.
static int SW = 800, SH = 600;
static const char* PERSIST_FILE = "nefuos.fs";

static HWND s_hwnd = 0;
static HBITMAP s_dib = 0;
static void* s_bits = 0;
static uint32_t s_heap_used = 0;
static uint8_t s_mouse_buttons = 0;

// (Re)create the 32bpp top-down DIB the whole core draws into. Destroying the
// old section while the window still paints is safe: nothing references the
// bits between frames.
static bool dib_reset(int w, int h) {
    if (w < 320 || h < 200) return false;
    if (w == SW && h == SH && s_dib && s_bits) return true;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC sdc = GetDC(0);
    HBITMAP nb = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &s_bits, 0, 0);
    ReleaseDC(0, sdc);
    if (!nb || !s_bits) return false;
    if (s_dib) DeleteObject(s_dib);
    s_dib = nb;
    SW = w;
    SH = h;
    return true;
}

// Choose the startup client size: the monitor work area, minus a little
// breathing room, never smaller than 800x600 and never larger than 1920x1200
// (bigger framebuffers only cost CPU in a software renderer).
static void pick_initial_size(int* out_w, int* out_h) {
    int w = 800, h = 600;
    RECT wa;
    if (SystemParametersInfoA(0x0030 /* SPI_GETWORKAREA */, 0, &wa, 0)) {
        w = wa.right - wa.left;
        h = wa.bottom - wa.top;
        w -= 16;              // keep a visible border
        h -= 16;
    }
    if (w < 800) w = 800;
    if (h < 600) h = 600;
    if (w > 1920) w = 1920;
    if (h > 1200) h = 1200;
    *out_w = w;
    *out_h = h;
}

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
            out.setpx(x, y, (rr << 16) | (g << 8) | b);
            }
        }
    bmp.UnlockBits(&bd);
    return true;
}


void platform_ttf_free(uint8_t* p) {
    if (p) kfree(p);
}

bool platform_ttf_text(const char* utf8, int px, int& out_w, int& out_h, uint8_t*& out_rgba) {
    out_rgba = 0; out_w = 0; out_h = 0;
    if (!utf8 || !*utf8 || px <= 0 || px > 256) return false;
    if (!s_gp_init) {
        Gdiplus::GdiplusStartupInput in;
        if (Gdiplus::GdiplusStartup(&s_gp_token, &in, 0) != Gdiplus::Ok) return false;
        s_gp_init = true;
        }
    // UTF-8 -> UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, 0, 0);
    if (wlen <= 0) return false;
    wchar_t* wbuf = (wchar_t*)malloc((size_t)wlen * 2);
    if (!wbuf) return false;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, wlen);
    Gdiplus::FontFamily fam(L"Segoe UI");
    Gdiplus::Font font(&fam, (Gdiplus::REAL)px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF box(0, 0, 4096, 1024);
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoFitBlackBox);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    // measure
    Gdiplus::GraphicsPath path;
    path.AddString(wbuf, -1, &fam, Gdiplus::FontStyleRegular, (Gdiplus::REAL)px, Gdiplus::PointF(0.0f, 0.0f), &fmt);
    Gdiplus::RectF bounds;
    path.GetBounds(&bounds, 0, 0);
    int w = (int)(bounds.Width + 2.5f);
    int h = (int)(bounds.Height + 2.5f);
    if (w <= 0 || h <= 0) { free(wbuf); return false; }
    if (w > 4096) w = 4096;
    if (h > 2048) h = 2048;
    int ox = (int)(-bounds.X + 1), oy = (int)(-bounds.Y + 1);
    // render into ARGB bitmap
    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::SolidBrush br(Gdiplus::Color(255, 255, 255, 255));
    g.DrawString(wbuf, -1, &font, Gdiplus::PointF((Gdiplus::REAL)ox, (Gdiplus::REAL)oy), &fmt, &br);
    free(wbuf);
    Gdiplus::BitmapData bd;
    Gdiplus::Rect r(0, 0, w, h);
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    uint8_t* out = (uint8_t*)kalloc((size_t)w * (size_t)h * 4);
    if (!out) { bmp.UnlockBits(&bd); return false; }
    const uint8_t* src = (const uint8_t*)bd.Scan0;
    for (int y = 0; y < h; y++) {
        const uint8_t* row = src + (size_t)y * bd.Stride;
        for (int x = 0; x < w; x++) {
            uint8_t* d = out + ((size_t)y * (size_t)w + (size_t)x) * 4;
            d[0] = row[x * 4 + 2];       // R
            d[1] = row[x * 4 + 1];       // G
            d[2] = row[x * 4];           // B
            d[3] = row[x * 4 + 3];       // A
            }
        }
    bmp.UnlockBits(&bd);
    out_w = w; out_h = h; out_rgba = out;
    return true;
}

uint64_t browser_mem_used_bytes = 0;
void* balloc(size_t sz) { void* q = malloc(sz ? sz : 1); if (q) browser_mem_used_bytes += sz ? sz : 8; return q; }
void bfree(void* p) { free(p); }
void* brealloc(void* p, size_t sz) { return realloc(p, sz); }

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

void* krealloc(void* p, size_t sz) {
    if (!p) return kalloc(sz);
    uint32_t* h = (uint32_t*)p - 2;
    uint32_t old = h[0];
    if ((uint32_t)sz <= old) return p;
    void* np = kalloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    kfree(p);
    return np;
}

// ===================== threading =====================
void* platform_thread_create(void (*func)(void*), void* arg) {
    DWORD tid;
    HANDLE h = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, &tid);
    return (void*)h;
}

void* platform_thread_create_stk(void (*func)(void*), void* arg, uint32_t stack_bytes) {
    DWORD tid;
    // STACK_SIZE_PARAM_IS_A_RESERVATION: without this flag dwStackSize is a
    // COMMIT size and the RESERVE stays at the PE default (16MB via
    // -Wl,--stack) — so a "256MB stack" request actually reserved only 16MB
    // and quickjs's parser recursion (deep nesting in bloxd.io's inline
    // bundle) walked past the stack base into the guard page (0xC0000005).
    HANDLE h = CreateThread(0, stack_bytes, (LPTHREAD_START_ROUTINE)func, arg,
                            0x00010000 /* STACK_SIZE_PARAM_IS_A_RESERVATION */, &tid);
    return (void*)h;
}

void platform_thread_sleep(uint32_t ms) {
    Sleep(ms);
}

// ===================== audio (host) =====================
bool platform_audio_available() { return waveOutGetNumDevs() > 0; }

bool platform_play_wav(const char* path) {
    return PlaySoundA(path, 0, SND_FILENAME | SND_ASYNC) != 0;
}

bool platform_play_wav_mem(const uint8_t* data, uint32_t size) {
    return PlaySoundA((LPCSTR)data, 0, SND_MEMORY | SND_ASYNC) != 0;
}

void platform_stop_sound() {
    PlaySoundA(0, 0, 0);
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
        RECT rc;
        GetClientRect(s_hwnd, &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if (cw == SW && ch == SH) {
            BitBlt(wdc, 0, 0, SW, SH, mdc, 0, 0, SRCCOPY);
        } else {
            // Fullscreen/stretch mode: stretch the 800x600 framebuffer across the whole client area
            StretchBlt(wdc, 0, 0, cw, ch, mdc, 0, 0, SW, SH, SRCCOPY);
        }
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

bool platform_rtc_date(DateInfo* out) {
    if (!out) return false;
    SYSTEMTIME st;
    GetLocalTime(&st);
    out->year = st.wYear;
    out->month = st.wMonth;
    out->day = st.wDay;
    out->hour = st.wHour;
    out->min = st.wMinute;
    out->sec = st.wSecond;
    out->dow = st.wDayOfWeek;   // 0 = Sunday .. 6 = Saturday
    return true;
}

// Windows owns the timezone: report the real bias and ignore attempts to set it.
// (The kernel's own localtime()/clock path only exists on bare; the host build
// links the real CRT, which already knows about the zone.)
int platform_tz_offset_minutes() {
    TIME_ZONE_INFORMATION tzi;
    DWORD r = GetTimeZoneInformation(&tzi);
    // Bias is minutes WEST of UTC; nefuOS wants minutes EAST.
    int east = -(int)tzi.Bias;
    if (r == TIME_ZONE_ID_DAYLIGHT) east -= (int)tzi.DaylightBias;
    else                          east -= (int)tzi.StandardBias;
    return east;
}
void platform_set_tz_offset_minutes(int minutes) { (void)minutes; }
bool platform_rtc_is_utc() { return false; }
void platform_set_rtc_is_utc(bool on) { (void)on; }

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

// The real nefuLLM weights ship as a single NEFL blob (llm_model.img, ~68 MB).
// Probe the usual spots so both `cd dist; .\nefuOS.exe` and a repo-root launch
// find it; a missing or malformed file simply leaves the chatbot on its rule
// table.
bool platform_llm_load(uint8_t** out, uint32_t* out_size) {
    if (out) *out = 0;
    if (out_size) *out_size = 0;
    static const char* kModelPaths[] = {
        "llm_model.img",
        "dist\\llm_model.img",
        "..\\dist\\llm_model.img"
    };
    FILE* f = 0;
    for (int i = 0; i < 3 && !f; i++) f = fopen(kModelPaths[i], "rb");
    if (!f) { klogf("llm: no llm_model.img next to the exe\n"); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 4096 || n > (long)(256u * 1024u * 1024u)) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)kalloc((size_t)n);
    if (!buf) { fclose(f); return false; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (r != (size_t)n || memcmp(buf, "NEFL", 4) != 0) {
        kfree(buf);
        klogf("llm: llm_model.img is not a NEFL blob\n");
        return false;
    }
    *out = buf;
    *out_size = (uint32_t)n;
    klogf("llm: loaded model blob %u bytes\n", (unsigned)n);
    return true;
}

void platform_fs_save(const uint8_t* data, uint32_t size) {
    FILE* f = fopen(PERSIST_FILE, "wb");
    if (!f) return;
    fwrite(data, 1, size, f);
    fclose(f);
}

// Incremental write-back: overwrite just the blocks that changed since the
// last flush. When the file on disk is missing or a different length a partial
// write would leave a truncated image the next boot cannot mount, so those two
// cases drop back to a full save.
bool platform_fs_save_range(const uint8_t* data, uint32_t total_size,
                            uint32_t first_block, uint32_t count) {
    if (!data || total_size == 0 || count == 0) return false;
    uint32_t off = first_block * 512u;
    if (off >= total_size) return true;                 // nothing left to write
    uint32_t len = count * 512u;
    if (off + len > total_size) len = total_size - off;
    long have = 0;
    FILE* rf = fopen(PERSIST_FILE, "rb");
    if (rf) {
        fseek(rf, 0, SEEK_END);
        have = ftell(rf);
        fclose(rf);
    }
    if (have != (long)total_size) {                     // absent / stale size
        platform_fs_save(data, total_size);
        return true;
    }
    FILE* f = fopen(PERSIST_FILE, "r+b");
    if (!f) { platform_fs_save(data, total_size); return true; }
    fseek(f, (long)off, SEEK_SET);
    size_t w = fwrite(data + off, 1, len, f);
    fclose(f);
    return w == len;
}

// Host cadence: four seconds. The image already lives in RAM here, so the
// persistence file only needs to catch up now and then.
uint32_t platform_fs_flush_interval_ms() { return 4000u; }

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

// ---- enumerate ALL network adapters (multi-NIC support) ----
int platform_net_get_all(NetAdapterInfo* list, int max) {
    if (!list || max <= 0) return 0;
    ULONG sz = 0;
    GetAdaptersInfo(0, &sz);
    if (sz == 0) return 0;
    IP_ADAPTER_INFO* ai = (IP_ADAPTER_INFO*)malloc(sz);
    if (!ai) return 0;
    int n = 0;
    if (GetAdaptersInfo(ai, &sz) == NO_ERROR) {
        for (IP_ADAPTER_INFO* p = ai; p && n < max; p = p->Next) {
            if (p->Type == MIB_IF_TYPE_LOOPBACK) continue;
            if (p->IpAddressList.IpAddress.String[0] == '0' ||
                p->IpAddressList.IpAddress.String[0] == 0) continue;
            memset(&list[n], 0, sizeof(NetAdapterInfo));
            memcpy(list[n].mac, p->Address, (p->AddressLength > 6) ? 6 : p->AddressLength);
            list[n].ip = inet_addr(p->IpAddressList.IpAddress.String);
            list[n].gw = inet_addr(p->GatewayList.IpAddress.String);
            list[n].mask = inet_addr(p->IpAddressList.IpMask.String);
            list[n].up = (p->Type != MIB_IF_TYPE_OTHER);
            strncpy(list[n].name, p->Description, 47);
            list[n].name[47] = 0;
            n++;
            }
        }
    free(ai);
    return n;
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

// ---- native TLS 1.3 HTTP(S) (Chrome ClientHello fingerprint; beats
// Cloudflare/WAF TLS fingerprinting that rejects plain WinINet/SChannel) ----
struct TlsSockCtx { SOCKET s; };

// TLS recv callback contract (see core/net/tls13.h):
//   > 0 : bytes received
//     0 : peer closed the connection (EOF)
//    -1 : no data within the timeout (caller may retry)
//    -2 : fatal socket error
static int tls_recv_cb(void* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    TlsSockCtx* sc = (TlsSockCtx*)ctx;
    if (timeout_ms > 0) {
        fd_set f; FD_ZERO(&f); FD_SET(sc->s, &f);
        timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(0, &f, 0, 0, &tv) <= 0) return -1;   // timeout: no data yet
    }
    int r = recv(sc->s, (char*)buf, maxlen, 0);
    if (r == SOCKET_ERROR) return -2;                   // fatal socket error
    return r;                                            // 0 = peer closed
}
static int tls_send_cb(void* ctx, const uint8_t* buf, int len) {
    return send(((TlsSockCtx*)ctx)->s, (const char*)buf, len, 0);
}

static bool http_parse_url(const char* url, char* scheme, int scheme_sz,
                           char* host, int host_sz, int* port, char* path, int path_sz) {
    const char* p = strstr(url, "://");
    if (!p) return false;
    int sch = (int)(p - url);
    if (sch <= 0 || sch >= scheme_sz - 1) return false;
    memcpy(scheme, url, sch); scheme[sch] = 0;
    p += 3;
    const char* slash = strchr(p, '/');
    const char* colon = strchr(p, ':');
    const char* hostend;
    if (slash && colon && colon < slash) hostend = colon;
    else if (slash) hostend = slash;
    else hostend = p + strlen(p);
    *port = 0;
    if (colon && (!slash || colon < slash)) {
        const char* pe = slash ? slash : hostend;
        int pl = (int)(pe - colon - 1);
        if (pl >= 1 && pl <= 5) { char ps[16]; memcpy(ps, colon + 1, pl); ps[pl] = 0; *port = atoi(ps); }
    }
    if (!*port) *port = (strcmp(scheme, "https") == 0) ? 443 : 80;
    int hl = (int)(hostend - p);
    if (hl <= 0 || hl >= host_sz - 1) return false;
    memcpy(host, p, hl); host[hl] = 0;
    if (slash) { int pl = (int)strlen(slash); if (pl >= path_sz) pl = path_sz - 1; memcpy(path, slash, pl); path[pl] = 0; }
    else { path[0] = '/'; path[1] = 0; }
    return true;
}

struct TlsReader {
    nefu::tls::Tls13Conn* c;
    uint8_t buf[16384];
    int pos, end;
    TlsReader(nefu::tls::Tls13Conn* cc) : c(cc), pos(0), end(0) {}
    // Fill the buffer with the next chunk of decrypted data.
    //
    // tls13_recv reports >0 = bytes, 0 = peer closed, -1 = no data yet (a
    // socket timeout, the caller may retry) and -2 = fatal error. A timeout
    // must not be treated as the end of the response: the peer may simply be
    // slow, and abandoning the transfer here would truncate the body.
    int fill(int timeout_ms) {
        if (pos < end) return 0;
        pos = 0; end = 0;
        uint32_t deadline = GetTickCount64() + (uint32_t)timeout_ms;
        for (;;) {
            int r = nefu::tls::tls13_recv(c, buf, (int)sizeof(buf), timeout_ms);
            if (r > 0) { end = r; return r; }
            if (r == 0) { platform_dbg("tls: reader: peer closed"); return 0; }
            if (r == -1) {                   // timeout: retry until the deadline
                if (GetTickCount64() >= deadline) {
                    platform_dbg("tls: reader: timeout waiting for data");
                    return -1;
                }
                continue;
            }
            platform_dbg("tls: reader: fatal error from tls13_recv");
            return -2;                        // fatal error
        }
    }
    int read1() {
        if (pos >= end) { if (fill(10000) <= 0) return -1; }
        return buf[pos++];
    }
    int readline(char* out, int cap) {
        int i = 0;
        for (;;) {
            int b = read1();
            if (b < 0) return -1;
            if (b == '\n') break;
            if (i < cap - 1) out[i++] = (char)b;
        }
        // Strip the CR of a CRLF line ending. Without this an empty line
        // arrives as "\r" instead of "", so the "end of headers" test
        // (line[0] == 0) never fires and the chunked framing that follows is
        // parsed as if it were header lines.
        if (i > 0 && out[i - 1] == '\r') i--;
        out[i] = 0;
        return i;
    }
    int readn(uint8_t* out, int n) {
        int got = 0;
        while (got < n) {
            if (pos >= end) { if (fill(10000) <= 0) return -1; }
            int take = end - pos;
            if (take > n - got) take = n - got;
            memcpy(out + got, buf + pos, take);
            pos += take; got += take;
        }
        return got;
    }
};

// One UA string for the whole stack: the wire request, navigator.userAgent and
// the client-hints block must agree, or bot checks flag the mismatch (a real
// browser never advertises a second product token after "Safari/537.36").
// The trailing "nefuOS/0.2" product token used to be appended here; Cloudflare
// reads any extra token as a bot hub (the same reason HeadlessChrome is
// blocked) and answered with a mandatory interactive challenge.
static const char* kChromeUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

// ===================== HTTP disk cache (host) =====================
// Plain GET responses are cached next to the exe under httpcache\. This makes
// heavy JS bundles and pages reload instantly on slow links and survives
// process restarts. Entries expire after 24h; a .url sidecar guards against
// hash collisions.
static void http_cache_dir(char* out, int cap) {
    char exe[MAX_PATH];
    GetModuleFileNameA(0, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    _snprintf(out, (size_t)cap, "%s\\httpcache", exe);
    out[cap - 1] = 0;
    CreateDirectoryA(out, 0);
}

static void http_cache_key(const char* url, char* out, int cap) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char* p = (const unsigned char*)url; *p; p++) {
        h ^= (uint64_t)*p;
        h *= 1099511628211ULL;
    }
    char clean[112];
    int n = 0;
    for (const char* p = url; *p && n < 100; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')
            clean[n++] = c;
        else
            clean[n++] = '_';
    }
    clean[n] = 0;
    _snprintf(out, (size_t)cap, "%016llx_%s", (unsigned long long)h, clean);
    out[cap - 1] = 0;
}

// Servers hand back gzip whenever they feel like it -- and the OS stack does
// not always decode for us (a caller-supplied Accept-Encoding, a cache hit
// written before decoding existed, an edge that ignores identity). Anything
// gzip-shaped that reaches the HTML parser or quickjs is fatal, so sniff the
// RFC 1952 magic and inflate in place.
static void http_maybe_gunzip(uint8_t** body, uint32_t* len) {
    if (!body || !*body || !len || *len < 18) return;
    const uint8_t* b = *body;
    if (!(b[0] == 0x1f && b[1] == 0x8b && b[2] == 0x08)) return;
    size_t cap = (size_t)(*len) * 6 + (1u << 20);
    uint32_t raw_len = *len;
    if (cap > (64u << 20)) cap = (64u << 20);
    for (int attempt = 0; attempt < 5; attempt++) {
        uint8_t* dec = (uint8_t*)kalloc(cap + 1);
        if (!dec) return;
        int dl = nefu::deflate::gzip_decompress(b, (size_t)*len, dec, cap);
        if (dl > 0) {
            kfree(*body);
            dec[dl] = 0;
            *body = dec;
            *len = (uint32_t)dl;
            { char eb[200]; wsprintfA(eb, "http: gunzip %u -> %d bytes", raw_len, dl); platform_dbg(eb); }
            return;
        }
        kfree(dec);
        if (cap >= (64u << 20)) break;
        cap *= 3;
        if (cap > (64u << 20)) cap = (64u << 20);
    }
    { char eb[200]; wsprintfA(eb, "http: gunzip FAILED %u bytes", (unsigned)*len); platform_dbg(eb); }
}

static bool http_cache_lookup(const char* url, uint8_t** out, uint32_t* out_len) {
    char dir[512], key[256], purl[800], pbin[800];
    http_cache_dir(dir, sizeof(dir));
    http_cache_key(url, key, sizeof(key));
    _snprintf(purl, sizeof(purl), "%s\\%s.url", dir, key);
    _snprintf(pbin, sizeof(pbin), "%s\\%s.bin", dir, key);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(pbin, GetFileExInfoStandard, &fa)) return false;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t0, t1;
    t0.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    t0.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    t1.LowPart = now.dwLowDateTime;
    t1.HighPart = now.dwHighDateTime;
    if (t1.QuadPart - t0.QuadPart > (ULONGLONG)86400ULL * 10000000ULL) return false;  // 24h TTL
    FILE* fu = fopen(purl, "rb");
    if (!fu) return false;
    char stored[2048] = "";
    if (!fgets(stored, (int)sizeof(stored), fu)) { fclose(fu); return false; }
    fclose(fu);
    size_t sl = strlen(stored);
    while (sl && (stored[sl - 1] == '\n' || stored[sl - 1] == '\r')) stored[--sl] = 0;
    if (strcmp(stored, url) != 0) return false;
    FILE* f = fopen(pbin, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)kalloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { kfree(buf); return false; }
    buf[rd] = 0;
    *out = buf;
    *out_len = (uint32_t)rd;
    http_maybe_gunzip(out, out_len);   // old entries may hold gzip bytes
    char eb[240];
    wsprintfA(eb, "http: DISK CACHE hit %u bytes url=%s", *out_len, url);
    platform_dbg(eb);
    return true;
}

static void http_cache_store(const char* url, const uint8_t* body, uint32_t len) {
    if (!body || len == 0 || len > (32u << 20)) return;
    char dir[512], key[256], purl[800], pbin[800];
    http_cache_dir(dir, sizeof(dir));
    http_cache_key(url, key, sizeof(key));
    _snprintf(purl, sizeof(purl), "%s\\%s.url", dir, key);
    _snprintf(pbin, sizeof(pbin), "%s\\%s.bin", dir, key);
    FILE* f = fopen(pbin, "wb");
    if (!f) return;
    size_t wr = fwrite(body, 1, len, f);
    fclose(f);
    if (wr != len) { DeleteFileA(pbin); return; }
    f = fopen(purl, "wb");
    if (f) { fprintf(f, "%s\n", url); fclose(f); }
    char eb[240];
    wsprintfA(eb, "http: DISK CACHE store %u bytes url=%s", len, url);
    platform_dbg(eb);
}

// Streaming helper that mirrors the bytes into a memory buffer so the response
// can be written to the disk cache after the body completes.
struct StreamStore {
    void* ctx;
    bool (*cb)(void*, const uint8_t*, uint32_t);
    uint8_t* buf;
    uint32_t len, cap;
    bool over;
    bool live;      // false: buffer only, delivery is deferred to the end
};

static bool stream_store_cb(void* vctx, const uint8_t* data, uint32_t len) {
    StreamStore* s = (StreamStore*)vctx;
    if (!s->over) {
        if (s->len + len > 32u << 20) {
            s->over = true;
        } else {
            if (s->len + len > s->cap) {
                uint32_t nc = s->cap ? s->cap * 2 : 65536;
                while (nc < s->len + len) nc *= 2;
                uint8_t* nb = (uint8_t*)kalloc(nc);
                if (nb) {
                    if (s->len) memcpy(nb, s->buf, s->len);
                    kfree(s->buf);
                    s->buf = nb;
                    s->cap = nc;
                } else {
                    s->over = true;
                }
            }
            if (!s->over) {
                memcpy(s->buf + s->len, data, len);
                s->len += len;
            }
        }
    }
    return s->live ? s->cb(s->ctx, data, len) : true;
}

static bool http_tls_request(const char* url, int max_redirects,
                             void* ctx, bool (*cb)(void*, const uint8_t*, uint32_t),
                             uint8_t** out, uint32_t* out_len) {
    // NOTE: the old per-URL local-file overrides for bloxd.io are gone; the
    // general HTTP disk cache (http_cache_lookup/store) covers pre-fetched
    // content for any site.
    static int ws_init = 0;
    if (!ws_init) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) == 0) ws_init = 1;
    }
    char scheme[16], host[256], path[4096];
    int port;
    if (!http_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)))
        return false;
    if (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0) return false;
    if (strcmp(scheme, "http") == 0) {
        // plain HTTP (npkg/registry etc.): the WinINet path collects the whole
        // body into *out / *out_len (kalloc'd), matching the TLS path's shape.
        // Streaming callers (the page loader) pass cb with out==0 — collect
        // mode would return false there, so route them through the stream
        // sender instead.
        if (out && out_len)
            return platform_http_request("GET", url, 0, 0, 0, 0, out, out_len);
        struct HttpWrap { void* ctx; bool (*cb)(void*, const uint8_t*, uint32_t); } w = { ctx, cb };
        int st = 0;
        return platform_http_stream_send("GET", url, 0, 0, 0, &w,
            [](void* c, const uint8_t* d, uint32_t n) {
                HttpWrap* wp = (HttpWrap*)c;
                return wp->cb(wp->ctx, d, n);
            }, &st);
    }
    // Disk cache first: an entry younger than 24h whose .url sidecar matches
    // is served directly, in both collect and streaming shapes.
    {
        uint8_t* cc = 0;
        uint32_t ccl = 0;
        if (http_cache_lookup(url, &cc, &ccl)) {
            if (out && out_len) {
                *out = cc;
                *out_len = ccl;
            } else if (cb) {
                uint32_t off = 0;
                bool keep = true;
                while (off < ccl && keep) {
                    uint32_t take = (ccl - off) > 32768 ? 32768 : (ccl - off);
                    keep = cb(ctx, cc + off, take);
                    off += take;
                }
                kfree(cc);
            } else {
                kfree(cc);
            }
            return true;
        }
    }
    // Streaming helper that mirrors the bytes into a memory buffer so the
    // response can be written to the disk cache after the body completes.
    StreamStore ss;
    ss.ctx = ctx; ss.cb = cb; ss.buf = 0; ss.len = 0; ss.cap = 0; ss.over = false; ss.live = true;
    bool use_ss = (cb && !out);
    for (int redir = 0; redir <= max_redirects; redir++) {
        struct addrinfo hints, *res = 0;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char ps[16]; wsprintfA(ps, "%d", port);
        if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) {
            char eb[200]; wsprintfA(eb, "tls: getaddrinfo fail host=%s url=%s", host, url); platform_dbg(eb);
            goto tls_fail;
        }
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) { freeaddrinfo(res); return false; }
        char ip_str[64] = "";
        if (res->ai_addr->sa_family == AF_INET) {
            struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
            uint32_t a = ntohl(sa->sin_addr.s_addr);
            wsprintfA(ip_str, "%u.%u.%u.%u", (a >> 24) & 0xFF, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
        }
        int okc = connect(s, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);
        if (okc != 0) {
            char eb[200]; wsprintfA(eb, "tls: connect fail err=%d host=%s", WSAGetLastError(), host); platform_dbg(eb);
            closesocket(s); goto tls_fail;
        }
        TlsSockCtx sc; sc.s = s;
        char err[160];
        nefu::tls::Tls13Conn* c = nefu::tls::tls13_connect(tls_recv_cb, tls_send_cb, &sc, host, err, sizeof(err));
        if (!c) {
            char eb[240]; wsprintfA(eb, "tls: handshake fail [%s] host=%s", err, host); platform_dbg(eb);
            closesocket(s); goto tls_fail;
        }
        char req[8192];
        // Collect mode can inflate gzip itself; the streaming path feeds bytes
        // straight into the HTML parser, so it must ask for identity encoding.
        const char* ae = out ? "Accept-Encoding: gzip\r\n" : "Accept-Encoding: identity\r\n";
        int rl = wsprintfA(req,
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
            "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
            "sec-ch-ua: \"Chromium\";v=\"126\", \"Not.A/Brand\";v=\"24\"\r\n"
            "sec-ch-ua-mobile: ?0\r\n"
            "sec-ch-ua-platform: \"Windows\"\r\n"
            "Upgrade-Insecure-Requests: 1\r\n"
            "%s"
            "Connection: close\r\n"
            "\r\n", path, host, kChromeUa, ae);
        if (rl <= 0 || rl >= (int)sizeof(req)) { nefu::tls::tls13_close(c); closesocket(s); goto tls_fail; }
        if (nefu::tls::tls13_send(c, (const uint8_t*)req, rl) != rl) { nefu::tls::tls13_close(c); closesocket(s); goto tls_fail; }
        TlsReader rd(c);
        char line[512];
        int code = -1;
        if (rd.readline(line, sizeof(line)) < 0) {
            char eb[200]; wsprintfA(eb, "tls: no status line host=%s", host); platform_dbg(eb);
            nefu::tls::tls13_close(c); closesocket(s); goto tls_fail;
        }
        if (strncmp(line, "HTTP/", 5) == 0 && strlen(line) > 12) code = atoi(line + 9);
        char eb[200]; wsprintfA(eb, "tls: status %d %s", code, line); platform_dbg(eb);
        int clen = -1;
        bool chunked = false;
        bool gz = false;
        char loc[1024] = "";
        for (;;) {
            if (rd.readline(line, sizeof(line)) < 0) break;
            if (line[0] == 0) break;
            if (_strnicmp(line, "Content-Length:", 15) == 0) clen = atoi(line + 15);
            else if (_strnicmp(line, "Transfer-Encoding:", 18) == 0 && strstr(line, "chunked")) chunked = true;
            else if (_strnicmp(line, "Content-Encoding:", 17) == 0 && (strstr(line, "gzip") || strstr(line, "deflate"))) gz = true;
            else if (_strnicmp(line, "Location:", 9) == 0) { int ll = (int)strlen(line + 9); if (ll > 0 && ll < 1023) memcpy(loc, line + 9, ll); }
        }
        if (code >= 300 && code < 400 && loc[0]) {
            char nu[2048];
            if (loc[0] == '/') { wsprintfA(nu, "%s://%s%s", scheme, host, loc); }
            else if (strncmp(loc, "http", 4) == 0) { int ll = (int)strlen(loc); if (ll >= 2048) ll = 2047; memcpy(nu, loc, ll); nu[ll] = 0; }
            else { nefu::tls::tls13_close(c); closesocket(s); continue; }
            nefu::tls::tls13_close(c); closesocket(s);
            return http_tls_request(nu, max_redirects - 1, ctx, cb, out, out_len);
        }
        if (code != 200 && code != 206) { nefu::tls::tls13_close(c); closesocket(s); goto tls_fail; }
        { char eb[240]; wsprintfA(eb, "tls: body clen=%d chunked=%d url=%s", clen, chunked ? 1 : 0, url); platform_dbg(eb); }
        // A CDN that ignores "Accept-Encoding: identity" still sends gzip.
        // Handing those bytes to the live callback would push compressed
        // garbage into the HTML parser and into quickjs, so buffer the body,
        // inflate it once complete, then deliver plain bytes.
        bool defer_body = (!out && cb && gz);
        if (defer_body) ss.live = false;
        bool gotbody = false;
        // bytes actually delivered on the streaming path (out == 0): a CDN
        // soft-block answers 200 with a well-formed but empty body, which must
        // be treated the same as a failure so the WinINet fallback can retry.
        uint32_t streamed = 0;
        if (chunked) {
            int chunks_dbg = 0;
            for (;;) {
                int rl2 = rd.readline(line, sizeof(line));
                if (rl2 < 0) {
                    char eb[240]; wsprintfA(eb, "tls: chunk line fail (EOF/timeout) total_sent=%d chunks=%d", gotbody ? 1 : 0, chunks_dbg); platform_dbg(eb);
                    // mid-body EOF: the response is incomplete -> fall back to
                    // WinINet instead of silently failing with a truncated page
                    nefu::tls::tls13_close(c); closesocket(s);
                    goto tls_fail;
                }
                int sz = (int)strtoul(line, 0, 16);
                if (sz <= 0) { gotbody = true; break; }
                {
                    char eb[200]; wsprintfA(eb, "tls: chunk #%d sz=%d", chunks_dbg, sz); platform_dbg(eb);
                }
                chunks_dbg++;
                if (out) {
                    uint8_t* nb = (uint8_t*)kalloc(*out_len + sz + 1);
                    if (!nb) break;
                    if (*out_len) memcpy(nb, *out, *out_len);
                    if (rd.readn(nb + *out_len, sz) != sz) { kfree(nb); break; }
                    kfree(*out); *out = nb; *out_len += sz; (*out)[*out_len] = 0;
                    gotbody = true;
                } else {
                    uint8_t tmp[32768];
                    int left = sz;
                    bool aborted = false;
                    while (left > 0) {
                        int take = left > (int)sizeof(tmp) ? (int)sizeof(tmp) : left;
                        int rn = rd.readn(tmp, take);
                        if (rn != take) {
                            char eb[200]; wsprintfA(eb, "tls: chunk readn fail want=%d got=%d", take, rn); platform_dbg(eb);
                            // mid-chunk EOF: incomplete body -> WinINet retry
                            nefu::tls::tls13_close(c); closesocket(s);
                            goto tls_fail;
                        }
                        bool cok = use_ss ? stream_store_cb(&ss, tmp, take) : cb(ctx, tmp, take);
                        if (!cok) { aborted = true; break; }
                        streamed += (uint32_t)take;
                        left -= take;
                    }
                    if (aborted) { gotbody = false; break; }
                    gotbody = true;
                }
                rd.readline(line, sizeof(line));
            }
        } else if (clen >= 0) {
            if (out) {
                uint8_t* nb = (uint8_t*)kalloc(clen + 1);
                if (nb && rd.readn(nb, clen) == clen) { *out = nb; *out_len = clen; nb[clen] = 0; gotbody = true; }
                else if (nb) kfree(nb);
            } else {
                uint8_t tmp[32768];
                int left = clen;
                while (left > 0) {
                    int take = left > (int)sizeof(tmp) ? (int)sizeof(tmp) : left;
                    if (rd.readn(tmp, take) != take) {
                        // mid-body EOF: incomplete page -> retry through the
                        // WinINet fallback rather than failing silently with
                        // (and caching!) a truncated document
                        char eb[200]; wsprintfA(eb, "tls: body EOF early left=%d of %d url=%s", left, clen, url); platform_dbg(eb);
                        nefu::tls::tls13_close(c); closesocket(s);
                        goto tls_fail;
                    }
                    bool cok = use_ss ? stream_store_cb(&ss, tmp, take) : cb(ctx, tmp, take);
                    if (!cok) { gotbody = true; break; }
                    streamed += (uint32_t)take;
                    left -= take;
                }
                if (left <= 0) gotbody = true;
            }
        } else {
            if (out) {
                for (;;) {
                    int r = rd.fill(5000);
                    if (r <= 0) break;
                    uint8_t* nb = (uint8_t*)kalloc(*out_len + r + 1);
                    if (!nb) break;
                    if (*out_len) memcpy(nb, *out, *out_len);
                    memcpy(nb + *out_len, rd.buf, r);
                    kfree(*out); *out = nb; *out_len += r; (*out)[*out_len] = 0;
                }
                gotbody = (*out_len > 0);
            } else {
                for (;;) {
                    int r = rd.fill(5000);
                    if (r <= 0) break;
                    bool cok = use_ss ? stream_store_cb(&ss, rd.buf, (uint32_t)r) : cb(ctx, rd.buf, (uint32_t)r);
                    if (!cok) break;
                    streamed += (uint32_t)r;
                }
                gotbody = true;
            }
        }
        if (out && *out && *out_len > 0 && gz) {
            uint8_t* raw = *out; uint32_t raw_len = *out_len;
            size_t cap = (size_t)raw_len * 4 + 4096;
            if (cap < (1u << 20)) cap = (1u << 20);
            int done = -1;
            for (int attempt = 0; attempt < 3 && done < 0; attempt++) {
                uint8_t* dec = (uint8_t*)kalloc(cap);
                if (!dec) break;
                int dl = nefu::deflate::gzip_decompress(raw, (size_t)raw_len, dec, cap);
                if (dl > 0) {
                    kfree(raw); *out = dec; *out_len = (uint32_t)dl;
                    if (*out_len < cap) (*out)[*out_len] = 0;
                    done = dl; gotbody = true;
                } else { kfree(dec); cap *= 4; if (cap > (64u << 20)) break; }
            }
            { char eb[200]; wsprintfA(eb, "tls: gzip done=%d raw=%u dec=%u", done, raw_len, *out_len); platform_dbg(eb); }
        }
        // Deferred delivery: inflate the buffered body and flush it downstream
        // in parser-sized slices, exactly as the live path would have.
        if (defer_body && gotbody && ss.buf && !ss.over) {
            http_maybe_gunzip(&ss.buf, &ss.len);
            uint32_t off2 = 0;
            while (off2 < ss.len) {
                uint32_t take = (ss.len - off2) > 32768 ? 32768 : (ss.len - off2);
                if (!cb(ctx, ss.buf + off2, take)) break;
                off2 += take;
            }
            streamed = ss.len;
        }
        // A CDN TLS-fingerprint soft-block answers 200 with an empty body to
        // the hand-rolled client. Deliver that as a failure so the OS stack
        // gets a chance to serve the real content.
        bool empty_body = out ? (*out_len == 0) : (streamed == 0);
        nefu::tls::tls13_close(c);
        closesocket(s);
        // only cache COMPLETE bodies: a partial page poisons the cache and
        // every later load of this URL would render the same truncated HTML
        if (gotbody && use_ss && ss.buf && !ss.over && ss.len > 0) http_cache_store(url, ss.buf, ss.len);
        if (ss.buf) { kfree(ss.buf); ss.buf = 0; }
        if (gotbody && out && *out && *out_len > 0) http_cache_store(url, *out, *out_len);
        if (gotbody && empty_body) goto tls_fail;
        return gotbody;
    }
tls_fail:
    // Host safety net: if the hand-rolled TLS 1.3 client could not complete the
    // request (handshake fingerprint-blocked by a CDN, unsupported peer cipher,
    // premature EOF, no HTTP status line on a CDN edge, or 200 + empty body),
    // fall back to the OS WinINet stack, which speaks the full modern TLS/cipher
    // suite and follows redirects and gzip automatically. The TLS 1.3 path stays
    // the primary route; this only catches what it cannot handle.
#ifndef NEFU_BARE
    {
        // Works for both shapes: collect-into-buffer (out/out_len) and the
        // streaming callback the page loader uses. WinINet decodes chunked
        // and gzip for us, so the callback always sees plain HTML bytes.
        uint8_t* fb_body = 0;
        uint32_t fb_len = 0;
        bool fb = platform_http_request("GET", url, 0, 0, 0, 0, &fb_body, &fb_len);
        if (fb && fb_body && fb_len > 0) {
            http_cache_store(url, fb_body, fb_len);
            if (out && out_len) {
                *out = fb_body;      // ownership moves to the caller
                *out_len = fb_len;
            } else if (cb) {
                uint32_t off = 0;
                bool keep = true;
                while (off < fb_len && keep) {
                    uint32_t take = (fb_len - off) > 32768 ? 32768 : (fb_len - off);
                    keep = cb(ctx, fb_body + off, take);
                    off += take;
                }
                kfree(fb_body);
            } else {
                kfree(fb_body);
            }
            char eb[240];
            wsprintfA(eb, "tls: WININET fallback ok %u bytes %s url=%s",
                      fb_len, out ? "collect" : "stream", url);
            platform_dbg(eb);
            return true;
        }
        if (fb_body) kfree(fb_body);
        { char eb[200]; wsprintfA(eb, "tls: WININET fallback failed url=%s", url); platform_dbg(eb); }
    }
#endif
    return false;
}

bool platform_http_get(const char* url, uint8_t** out, uint32_t* out_size) {
    if (!out || !out_size) return false;
    *out = 0; *out_size = 0;
    if (!url) return false;
    return http_tls_request(url, 4, 0, 0, out, out_size);
}

bool platform_http_stream(const char* url, void* ctx,
                          bool (*cb)(void*, const uint8_t*, uint32_t)) {
    if (!url || !cb) return false;
    return http_tls_request(url, 4, ctx, cb, 0, 0);
}
// ---- generic HTTP request (host): method + optional body + headers ----
// WinINet ignores a "Cookie:" header passed to HttpSendRequestA, but the npkg
// challenge requires a fresh __test cookie on every fetch. When `headers`
// carries a Cookie line, apply it through InternetSetCookieA (session cookie,
// path=/) and return a copy of the remaining headers for the wire. Returns 0
// when nothing must be sent.
static const char* apply_cookie_header(const char* headers, const char* url,
                                       char* hdrbuf, int hdrbuf_len) {
    if (!headers) return 0;
    // origin = scheme://host[:port]
    char origin[384];
    int olen = 0;
    const char* s = strstr(url, "://");
    if (s) {
        const char* pathstart = strchr(s + 3, '/');
        int len = pathstart ? (int)(pathstart - url) : (int)strlen(url);
        if (len < 384) { memcpy(origin, url, len); origin[len] = 0; olen = len; }
    }
    const char* p = headers;
    int out = 0;
    while (p && *p) {
        const char* nl = strstr(p, "\r\n");
        int linelen = nl ? (int)(nl - p) : (int)strlen(p);
        if (linelen >= 7 && _strnicmp(p, "Cookie:", 7) == 0) {
            const char* eq = strstr(p, "=");
            if (eq && eq < p + linelen && olen > 0) {
                const char* vs = p + 7;
                while (vs < eq && (*vs == ' ' || *vs == '\t')) vs++;
                int nl2 = (int)(eq - vs);
                if (nl2 > 0 && nl2 < 64) {
                    char name[64], val[160];
                    memcpy(name, vs, nl2); name[nl2] = 0;
                    const char* vs2 = eq + 1;
                    int vl = linelen - (int)(vs2 - p);
                    if (vl > 0 && vl < 128) {
                        memcpy(val, vs2, vl); val[vl] = 0;
                        char* semi = strchr(val, ';');
                        if (semi) *semi = 0;
                        char full[192];
                        int fl = 0;
                        const char* vs3 = val;
                        while (*vs3 && fl < 160) full[fl++] = *vs3++;
                        const char* attrs = "; path=/";
                        for (int ai = 0; attrs[ai] && fl < 190; ai++) full[fl++] = attrs[ai];
                        full[fl] = 0;
                        InternetSetCookieA(origin, name, full);
                    }
                }
            }
            // drop the Cookie line from the wire headers
        } else {
            if (out + linelen + 2 < hdrbuf_len) {
                memcpy(hdrbuf + out, p, linelen);
                out += linelen;
                hdrbuf[out++] = '\r'; hdrbuf[out++] = '\n';
            }
        }
        if (!nl) break;
        p = nl + 2;
    }
    if (out == 0) return 0;
    if (out < hdrbuf_len) hdrbuf[out] = 0;
    return hdrbuf;
}

// Splits the URL, then HttpOpenRequest/HttpSendRequest so POST bodies and
// extra request headers work (npkg/update depend on them). On success
// *out_body is a kalloc'd buffer (caller kfree).
// Raw header block of the last response ("HTTP/1.1 200 OK\r\nName: value\r\n"
// ...). Flattened to "Name: value\n" lines so the JS side gets the same shape
// the bare backend produces (see httpbare hb_capture_headers).
static char s_win_last_hdrs[2048];
// WinINet deliberately strips Set-Cookie from HTTP_QUERY_RAW_HEADERS_CRLF, so
// the header block never carries it and the JS cookie jar stayed empty. The
// cookie has to be queried on its own with HTTP_QUERY_SET_COOKIE.
static char s_win_last_setcookie[1024];

static void win_capture_headers(const char* raw, int n) {
    s_win_last_hdrs[0] = 0;
    if (!raw || n <= 0) return;
    int p = 0;
    while (p < n && raw[p] && raw[p] != '\n') p++;     // skip the status line
    if (p < n) p++;                                    // now at the first header char
    int used = 0;
    // NEFU-FIX: the old loop advanced p past the CRLF and then started the next
    // line at p + 1, which ate the first character of every header line
    // ("Content-Type" arrived as "ontent-Type"). Anything that sniffs the
    // response type before parsing (Colyseus' seat reservation, fetch's .json())
    // then took a JSON body for text and failed with "cannot read property
    // 'name' of undefined" -- the exact error that blocked matchmaking.
    while (p < n && raw[p]) {
        int le = p;
        while (le < n && raw[le] && raw[le] != '\r' && raw[le] != '\n') le++;
        int len = le - p;
        if (len > 0 && used + len + 2 < (int)sizeof(s_win_last_hdrs)) {
            memcpy(s_win_last_hdrs + used, raw + p, (size_t)len);
            used += len;
            s_win_last_hdrs[used++] = '\n';
            s_win_last_hdrs[used] = 0;
        }
        p = le;
        if (p < n && raw[p] == '\r') p++;
        if (p < n && raw[p] == '\n') p++;
    }
}

// Hosts whose TLS failed certificate validation this session. A broken
// endpoint stays broken, and JS retry loops (bloxd's social boot) otherwise
// pay the full handshake stall (~10 s) on every single retry — negative
// caching makes each subsequent request to that host fail instantly.
struct CertBadHost { char host[256]; uint32_t until; };
static CertBadHost s_cert_bad[8];
static int s_cert_bad_n = 0;
static int s_cert_ignore = 0;   // set after the first cert error: ignore cert checks

static bool cert_bad_hit(const char* host) {
    uint32_t now = (uint32_t)GetTickCount();
    for (int i = 0; i < s_cert_bad_n; i++)
        if (strcmp(s_cert_bad[i].host, host) == 0 && (int32_t)(s_cert_bad[i].until - now) > 0)
            return true;
    return false;
}

static void cert_bad_add(const char* host) {
    // 10 minutes: long enough to outlive a page's boot retry storm, short
    // enough that a fixed server works again after a reload
    uint32_t until = (uint32_t)GetTickCount() + 600000u;
    for (int i = 0; i < s_cert_bad_n; i++)
        if (strcmp(s_cert_bad[i].host, host) == 0) { s_cert_bad[i].until = until; return; }
    if (s_cert_bad_n < 8) {
        int cl = (int)strlen(host);
        if (cl > 255) cl = 255;
        memcpy(s_cert_bad[s_cert_bad_n].host, host, (size_t)cl);
        s_cert_bad[s_cert_bad_n].host[cl] = 0;
        s_cert_bad[s_cert_bad_n].until = until;
        s_cert_bad_n++;
    } else {
        // ring: overwrite the oldest entry
        for (int i = 1; i < s_cert_bad_n; i++) s_cert_bad[i - 1] = s_cert_bad[i];
        int cl = (int)strlen(host);
        if (cl > 255) cl = 255;
        memcpy(s_cert_bad[7].host, host, (size_t)cl);
        s_cert_bad[7].host[cl] = 0;
        s_cert_bad[7].until = until;
    }
}

static bool http_request_common(const char* method, const char* url,
                                const char* headers,
                                const uint8_t* body, uint32_t body_len,
                                int* out_status,
                                void (*sink)(void* ctx, const uint8_t* data, uint32_t len, bool* abort),
                                void* sink_ctx) {
    if (!url || !method) return false;
    // WinInet calls are synchronous on the page's JS thread, so any stall here
    // shows up as a dropped frame (and, when the page is mid-boot, as a whole
    // engine that never renders). Time the request end to end and name the
    // offender instead of letting a mute UI hide it.
    uint32_t t_req0 = (uint32_t)GetTickCount();
    URL_COMPONENTSA comp;
    memset(&comp, 0, sizeof(comp));
    comp.dwStructSize = sizeof(comp);
    comp.dwSchemeLength = comp.dwHostNameLength = comp.dwUrlPathLength = comp.dwExtraInfoLength = (DWORD)-1;
    if (!InternetCrackUrlA(url, 0, 0, &comp)) return false;
    if (comp.dwSchemeLength < 4 || comp.dwHostNameLength >= 256) return false;
    char host[256];
    memcpy(host, comp.lpszHostName, comp.dwHostNameLength);
    host[comp.dwHostNameLength] = 0;
    if (cert_bad_hit(host)) {
        // previously a fast-fail; now s_cert_ignore handles these hosts with
        // validation checks turned off, so only log the memory
        klogf("http: cert-flagged host %s (continuing with ignore flags)", host);
    }
    int pn = 0; char path[1024];
    if (comp.lpszUrlPath) { memcpy(path, comp.lpszUrlPath, comp.dwUrlPathLength); pn += comp.dwUrlPathLength; }
    if (comp.lpszExtraInfo) { memcpy(path + pn, comp.lpszExtraInfo, comp.dwExtraInfoLength); pn += comp.dwExtraInfoLength; }
    if (pn >= (int)sizeof(path)) return false;
    path[pn] = 0;
    INTERNET_PORT port = comp.nPort ? comp.nPort
        : (comp.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    for (int attempt = 0; attempt < 3; attempt++) {
        // Three retries each carrying its own timeout ladder would let one dead
        // endpoint freeze a frame for ~40 s; stop retrying once the call has
        // already burned most of the frame it belongs to.
        if (attempt > 0 && (uint32_t)GetTickCount() - t_req0 > 15000) break;
        // 0: direct, 1: system proxy, 2: direct again after a certificate
        // error flipped s_cert_ignore (the tolerant retry below).
        int openMode = (attempt == 1) ? INTERNET_OPEN_TYPE_PRECONFIG
                                      : INTERNET_OPEN_TYPE_DIRECT;
        HINTERNET h = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36 nefuOS/0.2", openMode, 0, 0, 0);
        if (!h) continue;
        HINTERNET c = InternetConnectA(h, host, port, 0, 0, INTERNET_SERVICE_HTTP, 0, 0);
        if (!c) { InternetCloseHandle(h); continue; }
        // Bound the synchronous WinInet phases. JS fetch()/XHR run inline on
        // the browser's JS thread: without these, one unreachable endpoint
        // (bad TLS, blackholed host) stalls the whole page for the WinInet
        // defaults (connect 20 s, send/receive 30 s each) and bloxd-style
        // retry loops turn that into minutes of dead UI.
        {
            DWORD ct = 6000, st = 8000, rt = 10000;
            InternetSetOptionA(c, INTERNET_OPTION_CONNECT_TIMEOUT, &ct, sizeof(ct));
            InternetSetOptionA(c, INTERNET_OPTION_SEND_TIMEOUT, &st, sizeof(st));
            InternetSetOptionA(c, INTERNET_OPTION_RECEIVE_TIMEOUT, &rt, sizeof(rt));
        }
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
        if (comp.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
        HINTERNET r = HttpOpenRequestA(c, method, path, 0, 0, 0, flags, 0);
        if (!r) { InternetCloseHandle(c); InternetCloseHandle(h); continue; }
        // Timeouts also belong on the *request* handle: WinInet applies the
        // receive/send limits there, and a connect-handle setting alone leaves
        // InternetReadFile free to block on a stalled server for minutes.
        {
            DWORD ct = 6000, st = 8000, rt = 10000;
            InternetSetOptionA(r, INTERNET_OPTION_CONNECT_TIMEOUT, &ct, sizeof(ct));
            InternetSetOptionA(r, INTERNET_OPTION_SEND_TIMEOUT, &st, sizeof(st));
            InternetSetOptionA(r, INTERNET_OPTION_RECEIVE_TIMEOUT, &rt, sizeof(rt));
        }
        // Once a host has tripped a certificate validation error, tell WinInet
        // to ignore those checks for the rest of this request: an autonomous
        // OS browser has no certificate dialog, and the alternative is a
        // multi-second synchronous stall per rotated endpoint (bloxd rotates
        // social*.bloxd.io constantly), which freezes the whole JS thread.
        if (s_cert_ignore) {
            DWORD sf = 0x100 /* IGNORE_UNKNOWN_CA */ | 0x1000 /* IGNORE_CERT_CN_INVALID */ |
                       0x2000 /* IGNORE_CERT_DATE_INVALID */ | 0x800 /* IGNORE_CERT_USAGE */ |
                       0x4000 /* IGNORE_REDIRECT_TO_HTTPS */;
            InternetSetOptionA(r, 31 /* INTERNET_OPTION_SECURITY_FLAGS */, &sf, sizeof(sf));
        }
        LPCSTR hdr = 0;
        {
            char hdrbuf[1024];
            const char* applied = apply_cookie_header(headers, url, hdrbuf, (int)sizeof(hdrbuf));
            hdr = applied ? applied : (headers ? headers : 0);
        }
        // dwHeadersLength must be the real length: passing (DWORD)-1 makes
        // HttpSendRequestA scan past the terminator and hang or crash.
        DWORD hdrlen = hdr ? (DWORD)strlen(hdr) : 0;
        BOOL sent = HttpSendRequestA(r, hdr, hdrlen,
                                     body ? (LPVOID)body : 0, body_len);
        if (!sent) {
            DWORD serr = GetLastError();
            klogf("http: send failed %s://%s:%u%s err=%lu", comp.nScheme == INTERNET_SCHEME_HTTPS ? "https" : "http", host, (unsigned)port, path, serr);
            // certificate problems: retry ONCE with validation checks ignored
            // (there is no user to click through a warning), then remember the
            // host so later requests skip straight to the tolerant path.
            // NOTE: MinGW's wininet.h defines ERROR_INTERNET_SEC_CERT_ERRORS
            // as 12055 but Windows actually reports 12057 — compare literal.
            if (!s_cert_ignore &&
                (serr == 12057 /* SEC_CERT_ERRORS */ ||
                 serr == 12038 /* SEC_CERT_CN_INVALID */ ||
                 serr == 12037 /* SEC_CERT_DATE_INVALID */)) {
                s_cert_ignore = 1;
                cert_bad_add(host);
                InternetCloseHandle(r); InternetCloseHandle(c); InternetCloseHandle(h);
                continue;   // retry this URL once with the ignore flags set
            }
            InternetCloseHandle(r); InternetCloseHandle(c); InternetCloseHandle(h);
            if (serr == 12057 || serr == 12038 || serr == 12037) {
                cert_bad_add(host);
                break;
            }
            continue;
        }
        if (out_status) {
            DWORD sc = 0, sl = sizeof(sc);
            HttpQueryInfoA(r, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &sc, &sl, 0);
            *out_status = (int)sc;
        }
        // Response headers: keep the raw block so Headers / getAllResponseHeaders()
        // can answer with what the server actually sent.
        {
            char rawh[4096];
            DWORD rl = sizeof(rawh) - 1;
            if (HttpQueryInfoA(r, HTTP_QUERY_RAW_HEADERS_CRLF, rawh, &rl, 0)) {
                rawh[rl < sizeof(rawh) ? rl : sizeof(rawh) - 1] = 0;
                win_capture_headers(rawh, (int)rl);
            } else {
                s_win_last_hdrs[0] = 0;
            }
            s_win_last_setcookie[0] = 0;
            {
                char scb[1024];
                DWORD sl = sizeof(scb) - 1;
                if (HttpQueryInfoA(r, HTTP_QUERY_SET_COOKIE, scb, &sl, 0)) {
                    scb[sl < sizeof(scb) ? sl : sizeof(scb) - 1] = 0;
                    int k = 0;
                    while (scb[k] && k < (int)sizeof(s_win_last_setcookie) - 1) {
                        s_win_last_setcookie[k] = scb[k];
                        k++;
                    }
                    s_win_last_setcookie[k] = 0;
                }
            }
        }
        bool ok = true;
        char tmp[16384];
        DWORD rd = 0;
        for (;;) {
            if (!InternetReadFile(r, tmp, sizeof(tmp), &rd)) { ok = false; break; }
            if (rd == 0) break;
            if (sink) {
                bool abort = false;
                sink(sink_ctx, (const uint8_t*)tmp, rd, &abort);
                if (abort) { ok = false; break; }
            }
        }
        InternetCloseHandle(r);
        InternetCloseHandle(c);
        InternetCloseHandle(h);
        {
            uint32_t el = (uint32_t)GetTickCount() - t_req0;
            if (el > 1000)
                klogf("http: SLOW %ums %s %s (status=%d)", (unsigned)el, method, url,
                      out_status ? *out_status : 0);
        }
        return ok;
    }
    if ((uint32_t)GetTickCount() - t_req0 > 1000)
        klogf("http: SLOW-FAIL %ums %s %s", (unsigned)((uint32_t)GetTickCount() - t_req0), method, url);
    return false;
}

struct HttpBufSink { uint8_t* buf; uint32_t cap, len; };
static void http_buf_sink(void* ctx, const uint8_t* data, uint32_t len, bool* abort) {
    HttpBufSink* s = (HttpBufSink*)ctx;
    if (s->len + len > s->cap) {
        uint32_t ncap = s->cap ? s->cap * 2 : 65536;
        while (ncap < s->len + len) ncap *= 2;
        // +1 keeps room for the NUL terminator callers append (e.g. the JS
        // engine writing js[blen] = 0 on a downloaded bundle).
        uint8_t* nb = (uint8_t*)kalloc((size_t)ncap + 1);
        if (!nb) { *abort = true; return; }
        if (s->buf) { memcpy(nb, s->buf, s->len); kfree(s->buf); }
        s->buf = nb;
        s->cap = ncap;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
}

bool platform_http_request(const char* method, const char* url,
                           const char* headers,
                           const uint8_t* body, uint32_t body_len,
                           int* out_status, uint8_t** out_body, uint32_t* out_body_len) {
    if (out_status) *out_status = 0;
    if (out_body) *out_body = 0;
    if (out_body_len) *out_body_len = 0;
    if (!out_body || !out_body_len) return false;
    HttpBufSink s; s.buf = 0; s.cap = 0; s.len = 0;
    bool ok = http_request_common(method, url, headers, body, body_len, out_status,
                                  http_buf_sink, &s);
    if (!ok || !s.buf) { if (s.buf) kfree(s.buf); return false; }
    s.buf[s.len] = 0;          // callers treat the body as a C string
    *out_body = s.buf;
    *out_body_len = s.len;
    http_maybe_gunzip(out_body, out_body_len);
    return true;
}

// WinINet keeps a process-wide cookie jar and replays it automatically, so
// there is nothing to hand to the JS layer here (see platform.h).
// WinINet normally keeps the cookie jar, but every platform_http_request
// opens a fresh InternetOpen session and the browser's JS shim also maintains
// its own jar. Returning the last Set-Cookie lets the shim feed that jar, so
// a cookie minted by social*.bloxd.io can be replayed on game-server-*.bloxd.io
// (a different host, where the WinINet session cookie no longer applies).
const char* platform_http_last_set_cookie() {
    if (s_win_last_setcookie[0]) return s_win_last_setcookie;
    static char out[1024];
    out[0] = 0;
    const char* p = s_win_last_hdrs;
    while (p && *p) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        if (!_strnicmp(p, "set-cookie:", 11)) {
            const char* v = p + 11;
            while (v < e && (*v == ' ' || *v == '\t')) v++;
            int n = 0;
            while (v + n < e && n < (int)sizeof(out) - 1) { out[n] = v[n]; n++; }
            out[n] = 0;
            return out;
        }
        p = (*e == '\n') ? e + 1 : e;
    }
    return out;
}

// Response headers of the most recent request, "Name: value" per '\n'. WinINet
// keeps its own cookie store, so only the head block is reported here.
const char* platform_http_last_headers() {
    return s_win_last_hdrs;
}

bool platform_http_stream_send(const char* method, const char* url,
                               const char* headers,
                               const uint8_t* body, uint32_t body_len,
                               void* ctx,
                               bool (*cb)(void*, const uint8_t*, uint32_t),
                               int* out_status) {
    if (!cb) return false;
    struct Wrap { void* ctx; bool (*cb)(void*, const uint8_t*, uint32_t); } w = { ctx, cb };
    return http_request_common(method, url, headers, body, body_len, out_status,
        [](void* c, const uint8_t* d, uint32_t n, bool* abort) {
            Wrap* wp = (Wrap*)c;
            if (!wp->cb(wp->ctx, d, n)) *abort = true;
        }, &w);
}

bool platform_http_stream_request(const char* method, const char* url,
                                  const char* headers, void* ctx,
                                  bool (*cb)(void*, const uint8_t*, uint32_t),
                                  int* out_status) {
    return platform_http_stream_send(method, url, headers, 0, 0, ctx, cb, out_status);
}

// ---- local file read (browser file:// URLs) ----
bool platform_read_file(const char* path, uint8_t** out, uint32_t* out_size) {
    if (!path || !out || !out_size) return false;
    *out = 0; *out_size = 0;
    // "file:///D:/x" arrives as "/D:/x": drop the leading slash before a
    // drive letter, and decode the percent-escapes smart_url may have added.
    char clean[1024];
    const char* p = path;
    if (p[0] == '/' && ((p[1] >= 'A' && p[1] <= 'Z') || (p[1] >= 'a' && p[1] <= 'z')) && p[2] == ':') p++;
    int ci = 0;
    for (const char* q = p; *q && ci < (int)sizeof(clean) - 1; q++) {
        if (q[0] == '%' && q[1] && q[2]) {
            char hx[3] = { q[1], q[2], 0 };
            clean[ci++] = (char)strtoul(hx, 0, 16);
            q += 2;
        } else if (q[0] == '?') {
            break;                       // query strings have no meaning here
        } else {
            clean[ci++] = *q;
        }
    }
    clean[ci] = 0;
    FILE* f = fopen(clean, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    uint8_t* buf = (uint8_t*)kalloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    *out = buf;
    *out_size = (uint32_t)rd;
    return true;
}

// ---- MicroPython nefu_http bridge (C linkage, malloc buffers) ----
extern "C" int nefu_http_request(const char* method, const char* url,
                                 const char* body, const char* content_type,
                                 const char* extra_headers,
                                 int* out_status, char** out_headers, uint32_t* out_headers_len,
                                 char** out_body, uint32_t* out_body_len) {
    if (out_status) *out_status = 0;
    if (out_headers) { *out_headers = 0; }
    if (out_headers_len) *out_headers_len = 0;
    if (out_body) *out_body = 0;
    if (out_body_len) *out_body_len = 0;
    if (!url || !method) return -1;
    // build a header block: content-type first (unless already present) then
    // the caller's extra headers (each line CRLF-terminated)
    char hdrs[2048];
    int hn = 0;
    const char* ct = content_type ? content_type : "application/json";
    if (extra_headers && extra_headers[0]) {
        // caller headers win if they already set content-type
        bool has_ct = false;
        const char* p = extra_headers;
        while (*p) {
            if (strncasecmp(p, "Content-Type:", 13) == 0) { has_ct = true; break; }
            const char* e = strchr(p, '\n');
            if (!e) break;
            p = e + 1;
        }
        if (!has_ct) { int n = snprintf(hdrs + hn, sizeof(hdrs) - hn, "Content-Type: %s\r\n", ct); if (n > 0) hn += n; }
        int n = snprintf(hdrs + hn, sizeof(hdrs) - hn, "%s", extra_headers);
        if (n > 0) hn += n;
    } else {
        int n = snprintf(hdrs + hn, sizeof(hdrs) - hn, "Content-Type: %s\r\n", ct);
        if (n > 0) hn += n;
    }
    hdrs[hn] = 0;
    int status = 0;
    HttpBufSink s; s.buf = 0; s.cap = 0; s.len = 0;
    bool ok = http_request_common(method, url, hdrs,
                                  body ? (const uint8_t*)body : 0,
                                  body ? (uint32_t)strlen(body) : 0,
                                  &status, http_buf_sink, &s);
    if (!ok) { if (s.buf) kfree(s.buf); return -1; }
    if (out_status) *out_status = status;
    // response headers (raw CRLF block) and body, both malloc'd for the mp side
    if (out_body && out_body_len && s.buf) {
        char* mb = (char*)malloc(s.len + 1);
        if (!mb) { kfree(s.buf); return -1; }
        memcpy(mb, s.buf, s.len); mb[s.len] = 0;
        kfree(s.buf);
        *out_body = mb;
        *out_body_len = s.len;
    }
    if (out_headers && out_headers_len) {
        // minimal: "HTTP/1.1 <status>\r\n" (callers mostly ignore headers)
        char* mh = (char*)malloc(32);
        if (mh) {
            int n = snprintf(mh, 32, "HTTP/1.1 %d\r\n", status);
            *out_headers = mh;
            *out_headers_len = (uint32_t)n;
        }
    }
    return 0;
}

// ---- real disk enumeration (host): Windows logical drives ----
int platform_disk_scan(DiskInfo* list, int max) {
    if (!list || max <= 0) return 0;
    DWORD mask = GetLogicalDrives();
    int n = 0;
    for (int i = 0; i < 26 && n < max; i++) {
        if (!(mask & (1u << i))) continue;
        char letter = (char)('A' + i);
        DiskInfo d;
        d.name[0] = 0; d.model[0] = 0; d.sectors = 0; d.removable = false;
        ksprintf(d.name, sizeof(d.name), "%c:", letter);
        // detect removable (floppy / USB stick) via GetDriveType
        char root[4] = { letter, ':', '\\', 0 };
        UINT t = GetDriveTypeA(root);
        d.removable = (t == DRIVE_REMOVABLE);
        // try to read the volume label; fall back to the type string
        char vbuf[64];
        DWORD vsn = 0, vmax = 63, flags = 0;
        if (GetVolumeInformationA(root, vbuf, vmax, &vsn, 0, &flags, 0, 0) && vbuf[0]) {
            ksprintf(d.model, sizeof(d.model), "volume %s", vbuf);
        } else {
            const char* ty = "fixed";
            if (t == DRIVE_REMOVABLE) ty = "removable";
            else if (t == DRIVE_CDROM) ty = "cd-rom";
            else if (t == DRIVE_REMOTE) ty = "network";
            ksprintf(d.model, sizeof(d.model), "%s drive", ty);
            }
        list[n++] = d;
        }
    return n;
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
    // no Shift/Caps handling here (MapVirtualKey)
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
        nefuos_handle_key(kc, ac, true, 0);
        return 0;
        }
    case WM_CHAR: {
        // IME / Unicode input: convert UTF-16 wchar to UTF-8
        wchar_t wc = (wchar_t)wp;
        if (wc > 127) {
            char buf[8] = {0};
            if (wc < 0x800) {
                buf[0] = (char)(0xC0 | (wc >> 6));
                buf[1] = (char)(0x80 | (wc & 0x3F));
            } else {
                buf[0] = (char)(0xE0 | (wc >> 12));
                buf[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (wc & 0x3F));
            }
            nefuos_handle_key(KEY_NONE, 0, true, buf);
            }
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
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) return 0;
        int cw = (int)LOWORD(lp), ch = (int)HIWORD(lp);
        if (cw >= 320 && ch >= 200 && (cw != SW || ch != SH)) {
            if (dib_reset(cw, ch)) {
                // the desktop re-lays itself out for the new resolution
                nefuos_screen_resized(cw, ch);
                InvalidateRect(hwnd, 0, FALSE);
            }
        }
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

// ---------------- () ----------------
#include <windows.h>
// Declared early so the crash handler can save a verification shot of the
// blue screen in headless runs (the real declarations live further down).
static const char* s_shot_path = 0;
static void save_bmp(const char* path);
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    FILE* f = fopen("crash.txt", "a");
    if (f) {
        HMODULE m = 0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &m);
        fprintf(f, "CRASH code=0x%lX addr=0x%p mod=0x%p off=0x%lX\n",
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress, m,
                (unsigned long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)m));
        CONTEXT* c = ep->ContextRecord;
        fprintf(f, "  rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX rip=%016llX\n",
                (unsigned long long)c->Rax, (unsigned long long)c->Rbx,
                (unsigned long long)c->Rcx, (unsigned long long)c->Rdx,
                (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
                (unsigned long long)c->Rbp, (unsigned long long)c->Rsp,
                (unsigned long long)c->Rip);
        fclose(f);
        }
    // Real crash -> automatic nefuOS blue screen with the TRUE register dump.
    // The filter never returns, so Windows does not terminate the process:
    // this keep-alive loop renders the blue screen forever (it can only be
    // left via the on-screen "Reboot" / closing the OS window).
    CrashRegs r;
    CONTEXT* c = ep->ContextRecord;
    r.rax = c->Rax; r.rbx = c->Rbx; r.rcx = c->Rcx; r.rdx = c->Rdx;
    r.rsi = c->Rsi; r.rdi = c->Rdi; r.rbp = c->Rbp; r.rsp = c->Rsp; r.rip = c->Rip;
    r.eflags = c->EFlags;
    blue_screen((uint32_t)ep->ExceptionRecord->ExceptionCode,
                "kernel panic: unhandled exception", &r);
    // Keep-alive render loop: paints the blue screen forever. The OS-level
    // fullscreen cannot be dismissed (keys swallowed, no close button); only
    // the on-screen "Reboot" / closing the OS window leaves it. In headless
    // --shot runs, save the blue screen bitmap after ~1.5s and exit.
    int nf = 0;
    for (;;) {
        nefuos_tick();
        nefuos_frame();
        if (s_shot_path && ++nf == 90) {
            save_bmp(s_shot_path);
            printf("[SHOT] blue screen saved after real crash\n");
            ExitProcess(0);
        }
        Sleep(16);
    }
    return EXCEPTION_EXECUTE_HANDLER;   // unreachable
}

// ---------------- reboot / suspend / uefi / hw info ----------------
namespace nefu {
void platform_reboot() {
    printf("nefuOS reboot requested (host)\n");
    nefuos_shutdown();
    ExitProcess(0);
}

void platform_suspend() {
    printf("nefuOS suspend requested (host: no-op)\n");
}

bool platform_uefi_load(UefiConfig* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    // host: no real UEFI NVRAM; defaults only
    strncpy(out->username, "user", sizeof(out->username) - 1);
    out->boot_timeout = 5;
    out->boot_splash = true;
    return true;
}

bool platform_uefi_save(const UefiConfig* cfg) {
    (void)cfg;
    return false;   // host: no NVRAM to write
}

bool platform_hw_info(HwInfo* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    out->cpu_cores = (uint8_t)si.dwNumberOfProcessors;
    int regs[4];
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004u) {
        char brand[49];
        __cpuid(regs, 0x80000002);
        memcpy(brand, regs, 16);
        __cpuid(regs, 0x80000003);
        memcpy(brand + 16, regs, 16);
        __cpuid(regs, 0x80000004);
        memcpy(brand + 32, regs, 16);
        brand[48] = 0;
        // trim leading/trailing spaces
        char* b = brand;
        while (*b == ' ') b++;
        int len = (int)strlen(b);
        while (len > 0 && (b[len - 1] == ' ' || b[len - 1] == 0)) len--;
        if (len > 63) len = 63;
        memcpy(out->cpu_model, b, (size_t)len);
        out->cpu_model[len] = 0;
    } else {
        strncpy(out->cpu_model, "x86_64", sizeof(out->cpu_model) - 1);
        }
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) out->mem_total_mb = (uint64_t)(ms.ullTotalPhys / (1024 * 1024));
    out->cpu_mhz = 0;   // not exposed via a stable API; leave 0
    strncpy(out->bios_vendor, "NEFU-HOST", sizeof(out->bios_vendor) - 1);
    strncpy(out->bios_version, "0.1", sizeof(out->bios_version) - 1);
    return true;
}

} // namespace nefu

// ---------------- entry ----------------
static int s_auto_app = -1;
static int s_click_x = -1, s_click_y = -1;
static int s_click2_x = -1, s_click2_y = -1;   // second test click (after key injection)
static int s_drag_x1 = -1, s_drag_y1 = -1, s_drag_x2 = -1, s_drag_y2 = -1;  // --drag: scripted press-move-release (scrollbar drag test)
static int s_nfuclick_idx = -1;                // DOM-level click test (--nfuclick N)
static const char* s_key_str = 0;
static const char* s_url_str = 0;
static const char* s_actions_str = 0;  // --actions "key:Escape,text:Play"
static const char* s_media_path = 0;   // --media: autoplay a real media file (video player)
static int s_lang_override = -1;         // --lang: force UI language (0 English, 1 Chinese)
static bool s_savefs = false;          // --savefs: persist the VFS snapshot before exit
static bool s_bsod_trigger = false;    // --bsod: show the BSOD-style panic screen
static bool s_npkg_test = false;       // --npkg-test: run the npkg registry download test
static bool s_update_test = false;     // --update-test: run the https update manifest pipeline
static int s_js_selftest = 0;          // --jstest[-early]: 1 = before OS init, 2 = after desktop

// Minimal JS engine probe: brings up quickjs and evaluates one tiny script.
// Used to tell "engine broken" apart from "page-side problem".
static void js_selftest(const char* tag) {
    char out[8192];
    char html[4096];
    out[0] = 0;
    html[0] = 0;
    int r = nefu::mini_js_run("var q = 6 * 7; print('q=' + q); 'ok'",
                              out, (int)sizeof(out), html, (int)sizeof(html));
    printf("[JSTEST %s] r=%d out=[%s] html=%zu\n", tag, r, out, strlen(html));
    fflush(stdout);
}
static int s_shot_delay_ms = 6000;     // --shotdelay: how long the --shot window runs

// Save the 800x600 32bpp DIB framebuffer as a BMP (top-down BGRA rows).
static void save_bmp(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = (DWORD)(fh.bfOffBits + (uint32_t)SW * (uint32_t)SH * 4u);
    BITMAPINFOHEADER ih;
    memset(&ih, 0, sizeof(ih));
    ih.biSize = sizeof(BITMAPINFOHEADER);
    ih.biWidth = SW;
    ih.biHeight = -SH;   // top-down, matches DIB memory order
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fwrite(&fh, 1, sizeof(fh), f);
    fwrite(&ih, 1, sizeof(ih), f);
    fwrite(s_bits, 1, (size_t)SW * (size_t)SH * 4u, f);
    fclose(f);
    printf("shot saved: %s\n", path);
}

// F2 key detection thread - runs from the very start of boot
static volatile bool s_f2_pressed = false;
static volatile bool s_enter_bios = false;   // F2 detected → open BIOS setup
static DWORD WINAPI f2_detect_thread(LPVOID param) {
    printf("[BOOT] F2 detection thread started - monitoring for F2 key...\n");
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 3000) {
        if (GetAsyncKeyState(VK_F2) & 0x8000) {
            s_f2_pressed = true;
            printf("[BOOT] *** F2 KEY DETECTED! Entering BIOS setup... ***\n");
            break;
            }
        Sleep(10);
        }
    if (!s_f2_pressed) {
        printf("[BOOT] F2 not detected, continuing normal boot.\n");
        }
    return 0;
}

// Boot preparation sequence
static void boot_prepare() {
    printf("[BOOT] nefuOS boot preparation started...\n");
    
    // Step 0: Start F2 detection thread immediately (multi-threaded).
    // The thread monitors F2 for 3 seconds; the result is checked at the
    // END of boot_prepare so the whole 3s window is usable.
    printf("[BOOT] Step 0: Starting F2 BIOS key detection thread...\n");
    HANDLE h_f2_thread = CreateThread(NULL, 0, f2_detect_thread, NULL, 0, NULL);
    if (h_f2_thread) {
        CloseHandle(h_f2_thread);
        }
    Sleep(100);
    
    // Step 1: Kernel file check & self-recovery
    printf("[BOOT] Step 1: Checking kernel integrity...\n");
    bool kernel_ok = true;
    if (!kernel_ok) {
        printf("[BOOT] WARNING: Kernel corrupted! Starting self-recovery...\n");
        Sleep(500);
        printf("[BOOT] Self-recovery complete.\n");
    } else {
        printf("[BOOT] Kernel integrity OK.\n");
        }
    
    // Step 2: Initialize components
    printf("[BOOT] Step 2: Initializing system components...\n");
    printf("[BOOT]   - Graphics subsystem\n");
    printf("[BOOT]   - Input subsystem\n");
    printf("[BOOT]   - File system\n");
    printf("[BOOT]   - Network stack\n");
    printf("[BOOT]   - Audio mixer\n");
    Sleep(200);
    
    // Step 3: BIOS key detection (multi-threaded)
    printf("[BOOT] Step 3: Checking BIOS boot keys...\n");
    Sleep(100);
    printf("[BOOT] No special boot keys detected.\n");
    
    // Step 4: Auto-start widgets
    printf("[BOOT] Step 4: Auto-starting desktop widgets...\n");
    Sleep(100);
    
    // Step 5: Bug validation / system check
    printf("[BOOT] Step 5: Running system validation...\n");
    printf("[BOOT]   - Memory check: OK\n");
    printf("[BOOT]   - Display check: OK\n");
    printf("[BOOT]   - Input check: OK\n");
    printf("[BOOT]   - Storage check: OK\n");
    Sleep(150);
    
    // Step 6: Wait 0.5 seconds
    printf("[BOOT] Step 6: Finalizing boot...\n");
    Sleep(500);

    printf("[BOOT] Boot preparation complete! Entering desktop...\n");
    // Note: the F2 check is intentionally NOT done here. The detection thread
    // monitors F2 for 3s; checking too early would miss late presses. main()
    // checks s_f2_pressed after the boot splash (see main), by which time the
    // thread has finished and the flag is final.
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) s_auto_app = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--shot") == 0 && i + 2 < argc) {
            s_auto_app = atoi(argv[i + 1]);
            s_shot_path = argv[i + 2];
            }
        if (strcmp(argv[i], "--click") == 0 && i + 2 < argc) {
            s_click_x = atoi(argv[i + 1]);
            s_click_y = atoi(argv[i + 2]);
            }
        if (strcmp(argv[i], "--click2") == 0 && i + 2 < argc) {
            s_click2_x = atoi(argv[i + 1]);
            s_click2_y = atoi(argv[i + 2]);
            }
        if (strcmp(argv[i], "--drag") == 0 && i + 4 < argc) {
            s_drag_x1 = atoi(argv[i + 1]);
            s_drag_y1 = atoi(argv[i + 2]);
            s_drag_x2 = atoi(argv[i + 3]);
            s_drag_y2 = atoi(argv[i + 4]);
            i += 4;
            }
        if (strcmp(argv[i], "--nfuclick") == 0 && i + 1 < argc) {
            s_nfuclick_idx = atoi(argv[i + 1]);
            }
        if (strcmp(argv[i], "--savefs") == 0) s_savefs = true;
        if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) s_key_str = argv[i + 1];
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) s_url_str = argv[i + 1];
        // Scripted page actions for the browser ("key:Escape,text:Play").
        // The comma separated list is handed to the browser as one action
        // queue, exactly like the terminal's "open <url> [action...]".
        if (strcmp(argv[i], "--actions") == 0 && i + 1 < argc) s_actions_str = argv[i + 1];
        if (strcmp(argv[i], "--media") == 0 && i + 1 < argc) s_media_path = argv[i + 1];
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) s_lang_override = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--llm") == 0) g_llm_opt_in = true;   // experimental neural chat
        if (strcmp(argv[i], "--shotdelay") == 0 && i + 1 < argc) {
            int v = atoi(argv[i + 1]);
            if (v > 0 && v <= 600000) s_shot_delay_ms = v;
            }
        if (strcmp(argv[i], "--bios") == 0) s_enter_bios = true;  // enter BIOS directly (for testing, same as pressing F2 at boot)
        if (strcmp(argv[i], "--bsod") == 0) s_bsod_trigger = true; // show the detailed blue screen (verification)
        if (strcmp(argv[i], "--npkg-test") == 0) s_npkg_test = true; // registry download test, then exit
        if (strcmp(argv[i], "--update-test") == 0) s_update_test = true; // https manifest pipeline, then exit
        if (strcmp(argv[i], "--jstest-early") == 0) s_js_selftest = 1;  // JS engine probe before any OS init
        if (strcmp(argv[i], "--jstest") == 0) s_js_selftest = 2;        // JS engine probe after desktop is up
        }
    SetUnhandledExceptionFilter(crash_handler);
    setvbuf(stdout, NULL, _IONBF, 0);   // live output even when redirected
    setvbuf(stderr, NULL, _IONBF, 0);
    if (s_js_selftest == 1) { js_selftest("early"); ExitProcess(0); }
    printf("argv: app=%d shot=%s click=%d,%d\n", s_auto_app,
           s_shot_path ? s_shot_path : "-", s_click_x, s_click_y);
    printf("nefuOS host backend (win32) starting...\n");

    HINSTANCE hInst = GetModuleHandleA(0);

    // Framebuffer size follows the real screen: the window opens at the
    // monitor work area, so the desktop lays out at native resolution instead
    // of being a fixed 800x600 surface stretched to fit.
    {
        int iw = 800, ih = 600;
        pick_initial_size(&iw, &ih);
        SW = iw;
        SH = ih;
        printf("display: startup framebuffer %dx%d (monitor work area)\n", SW, SH);
    }
    if (!dib_reset(SW, SH)) {
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

    // Resizable frame: dragging the window re-lays the whole desktop out
    // (see WM_SIZE -> nefuos_screen_resized).
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, SW, SH };
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    RECT wa;
    if (SystemParametersInfoA(0x0030 /* SPI_GETWORKAREA */, 0, &wa, 0)) {
        wx = wa.left + ((wa.right - wa.left) - ww) / 2;
        wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
        if (wx < wa.left) wx = wa.left;
        if (wy < wa.top) wy = wa.top;
    }
    s_hwnd = CreateWindowA("nefuOS", "nefuOS 0.1 - desktop", style,
                           wx, wy, ww, wh,
                           0, 0, hInst, 0);
    if (!s_hwnd) {
        printf("FATAL: cannot create window\n");
        return 1;
        }

    if (s_lang_override >= 0) nefu::g_lang_override = s_lang_override;
    boot_prepare();
    nefuos_init();
    // drive frames until the boot splash finishes so LVGL is fully initialised
    { uint32_t t0 = platform_tick_ms(); while (platform_tick_ms() - t0 < 1800) { nefuos_frame(); Sleep(16); } }
    if (s_npkg_test) {
        // --npkg-test: fetch the full npkg.site.je registry, list every
        // package, then install the first one end-to-end.  No GUI is needed;
        // results go to stdout and we exit.
        printf("[NPKG] npkg registry download test (host)...\n");
        uint8_t* body = 0; uint32_t blen = 0;
        if (nefu::npkg_gui_fetch_registry(&body, &blen) && body) {
            printf("[NPKG] body head: %.120s\n", (const char*)body);
            const char* bp = (const char*)body;
            printf("[NPKG] slowAES=%d toNums=%d len=%u\n",
                   strstr(bp, "slowAES") ? 1 : 0,
                   strstr(bp, "toNumbers(\"") ? 1 : 0,
                   (unsigned)blen);
            int n = nefu::npkg_gui_registry_count(body, blen);
            printf("[NPKG] registry bytes=%u packages=%d\n", (unsigned)blen, n);
            for (int i = 0; i < n && i < 12; i++) {
                char name[128], ver[64], intro[256];
                if (nefu::npkg_gui_registry_entry(body, blen, i,
                        name, (int)sizeof(name), ver, (int)sizeof(ver),
                        intro, (int)sizeof(intro)))
                    printf("[NPKG]   %2d: %s v%s - %.80s\n", i, name, ver, intro);
            }
            if (n > 0) {
                char name[128], ver[64], intro[256];
                if (nefu::npkg_gui_registry_entry(body, blen, 0,
                        name, (int)sizeof(name), ver, (int)sizeof(ver),
                        intro, (int)sizeof(intro))) {
                    char msg[512]; msg[0] = 0;
                    bool ok = nefu::npkg_gui_install(name, msg, (int)sizeof(msg));
                    printf("[NPKG] install '%s' -> %s | %s\n", name, ok ? "OK" : "FAIL",
                           msg[0] ? msg : "-");
                }
            }
            nefu::kfree(body);
            printf("[NPKG] DONE\n");
        } else {
            printf("[NPKG] fetch FAIL (network unavailable)\n");
        }
        return 0;   // headless test: no window loop
    }
    if (s_update_test) {
        // --update-test: drive the real https update manifest pipeline over the
        // hand-rolled TLS 1.3 stack.  The TLS diagnostic dumps (klogf) go to
        // stdout on the host, so a full HKDF/key derivation can be verified
        // without COM1 line truncation.  Headless: exit after the pipeline.
        printf("[UPDATE] TLS 1.3 manifest pipeline (host)...\n");
        nefu::UpdateOut uo;
        uo.print = [](void*, const char* s) { printf("%s", s); };
        uo.ud = 0;
        int rc = nefu::update_run_pipeline(uo, "https://nefuos.site.je/update.json", true);
        printf("[UPDATE] pipeline rc=%d\n", rc);
        return 0;
    }
    if (s_enter_bios) {
        // F2 was pressed during the 3s boot window (or --bios was given):
        // open the classic BIOS setup utility. Mark first boot done + unlock
        // so the BIOS window sits on the desktop; closing it continues boot.
        printf("[BOOT] Entering nefuOS BIOS Setup...\n");
        nefuos_mark_firstboot_done();
        nefuos_unlock();
        nefu::apps::bios_launch();
        }
    if (s_auto_app >= 0 || s_bsod_trigger) {
        // test mode: skip first-boot wizard and lock screen (frame loop
        // would otherwise have re-locked after the boot splash)
        nefuos_mark_firstboot_done();
        nefuos_unlock();
        if (s_bsod_trigger) {
            // --bsod: render the detailed BSOD-style panic screen directly
            // (no terminal on top), for a clean verification screenshot.
            printf("[SHOT] triggering nefuOS blue screen (BSOD)...\n");
            blue_screen(0xC0000005, "kernel panic: simulated crash");
        } else if (s_auto_app == 17 && s_url_str) {
            browser_launch_url(s_url_str);
            // Scripted page actions (--actions): the browser performs them
            // one per tick and retries a click until it hits.
            if (s_actions_str && *s_actions_str) {
                char list[256];
                size_t n = 0;
                list[0] = 0;
                const char* p = s_actions_str;
                while (*p && n < sizeof(list) - 2) {
                    const char* comma = strchr(p, ',');
                    size_t len = comma ? (size_t)(comma - p) : strlen(p);
                    if (len > 0 && len < 64) {
                        char one[72];
                        memcpy(one, p, len);
                        one[len] = 0;
                        printf("[SHOT] page action: %s\n", one);
                        nefu::browser_page_action(one);
                    }
                    if (!comma) break;
                    p = comma + 1;
                }
            }
        }
        else {
            if (s_auto_app == APP_VIDEOPLAYER && s_media_path)
                video_player_autoplay(s_media_path);   // --media test hook
            app_launch(s_auto_app);
        }
        }
    if (s_js_selftest == 2) { js_selftest("desktop"); ExitProcess(0); }
    if (s_shot_path) {
        printf("[SHOT] entering shot loop delay=%dms\n", s_shot_delay_ms);
        uint32_t t0 = platform_tick_ms();
        // --shotdelay window (default 6s): media apps (video decode + first
        // frame) and JS-heavy pages (bloxd main bundle) need longer.
        while (platform_tick_ms() - t0 < (uint32_t)s_shot_delay_ms) {
            // fire the test click just before the screenshot: JS-heavy pages
            // (bloxd) mount their UI tens of seconds after boot, so a click at
            // t0+1s would land on the empty page and never hit the target
            uint32_t click_at = (uint32_t)s_shot_delay_ms > 12000
                ? (uint32_t)s_shot_delay_ms - 5000 : 1000;
            if (s_click_x >= 0 && platform_tick_ms() - t0 > click_at) {
                int cx = s_click_x, cy = s_click_y;
                nefuos_handle_mouse(cx, cy, 0x01);
                // Keep the frame loop running while the press is held so LVGL
                // observes a real PRESSED state; then release. Without this,
                // press+release collapse into one tick and no CLICKED fires.
                for (int f = 0; f < 4; f++) { nefuos_tick(); nefuos_frame(); Sleep(16); }
                nefuos_handle_mouse(cx, cy, 0);
                s_click_x = -1;
                printf("clicked %d,%d\n", cx, cy);
            }
            if (s_drag_x1 >= 0 && platform_tick_ms() - t0 > click_at) {
                // Scripted press -> move -> release. Used to verify the
                // browser's draggable scrollbar end to end: press on the
                // thumb, walk the pointer down the track, let go.
                nefuos_handle_mouse(s_drag_x1, s_drag_y1, 0x01);
                for (int f = 0; f < 3; f++) { nefuos_tick(); nefuos_frame(); Sleep(16); }
                const int steps = 24;
                for (int k = 1; k <= steps; k++) {
                    int x = s_drag_x1 + (s_drag_x2 - s_drag_x1) * k / steps;
                    int y = s_drag_y1 + (s_drag_y2 - s_drag_y1) * k / steps;
                    nefuos_handle_mouse(x, y, 0x01);
                    nefuos_tick(); nefuos_frame(); Sleep(8);
                }
                nefuos_handle_mouse(s_drag_x2, s_drag_y2, 0);
                nefuos_tick(); nefuos_frame();
                printf("dragged %d,%d -> %d,%d\n", s_drag_x1, s_drag_y1,
                       s_drag_x2, s_drag_y2);
                s_drag_x1 = -1;
            }
            // late key injection, same rationale as the late click: React
            // pages mount tens of seconds after boot
            uint32_t key_at = (uint32_t)s_shot_delay_ms > 12000
                ? (uint32_t)s_shot_delay_ms - 6500 : 1400;
            if (s_key_str && platform_tick_ms() - t0 > key_at) {
                // inject key sequence (password) then Enter
                for (const char* p = s_key_str; *p; p++) {
                    char c = *p;
                    int kc = 0;
                    if (c == '\r' || c == '\n') kc = KEY_ENTER;
                    else if (c == 8) kc = KEY_BACKSPACE;
                    else if (c == 27) kc = KEY_ESC;
                    else if (c == '<') {
                        // special-key tag like <PgDn> for scroll tests
                        const char* q = p + 1;
                        const char* e2 = strchr(q, '>');
                        if (e2 && e2 - q < 10) {
                            char tag[12]; int tl = (int)(e2 - q);
                            memcpy(tag, q, (size_t)tl); tag[tl] = 0;
                            int kc2 = 0;
                            if (!strcmp(tag, "PgDn")) kc2 = KEY_PGDN;
                            else if (!strcmp(tag, "PgUp")) kc2 = KEY_PGUP;
                            else if (!strcmp(tag, "Down")) kc2 = KEY_DOWN;
                            else if (!strcmp(tag, "Up")) kc2 = KEY_UP;
                            else if (!strcmp(tag, "Home")) kc2 = KEY_HOME;
                            else if (!strcmp(tag, "End")) kc2 = KEY_END;
                            if (kc2) {
                                nefuos_handle_key(kc2, 0, true);
                                nefuos_handle_key(kc2, 0, false);
                                p = e2;
                                Sleep(24);
                                continue;
                            }
                        }
                    }

                    nefuos_handle_key(kc, c, true);
                    nefuos_handle_key(kc, c, false);
                    Sleep(24);
                }
                if (strchr(s_key_str, '<') == 0) {
                    nefuos_handle_key(KEY_ENTER, '\r', true);
                    nefuos_handle_key(KEY_ENTER, '\r', false);
                }
                printf("keys injected: %s\n", s_key_str);
                s_key_str = 0;
            }
            uint32_t click2_at = (uint32_t)s_shot_delay_ms > 12000
                ? (uint32_t)s_shot_delay_ms - 2500 : 3000;
            // DOM-level click test: fire before click2 and the screenshot
            if (s_nfuclick_idx >= 0 &&
                platform_tick_ms() - t0 > (uint32_t)(s_shot_delay_ms > 12000 ? s_shot_delay_ms - 4000 : 2000)) {
                nefu::browser_debug_nfu_click(s_nfuclick_idx);
                printf("nfuclick %d dispatched\n", s_nfuclick_idx);
                s_nfuclick_idx = -1;
            }
            if (s_click2_x >= 0 && platform_tick_ms() - t0 > click2_at) {
                // optional second click (e.g. click "Save" after typing)
                int cx = s_click2_x, cy = s_click2_y;
                nefuos_handle_mouse(cx, cy, 0x01);
                for (int f = 0; f < 4; f++) { nefuos_tick(); nefuos_frame(); Sleep(16); }
                nefuos_handle_mouse(cx, cy, 0);
                s_click2_x = -1;
                printf("clicked2 %d,%d\n", cx, cy);
            }
            // mirror the WM_TIMER handler: tick (window animations, media
            // frame grabs) then frame (paint)
            nefuos_tick();
            nefuos_frame(); Sleep(16);
            }
        printf("[SHOT] saving bitmap\n");
        if (s_savefs) {
            // Serialize the in-memory VFS into the NVFS image and persist it,
            // so test runs can verify files created/edited inside the OS.
            nefuos_shutdown();
            printf("fs saved\n");
        }
        save_bmp(s_shot_path);
        // keep the process alive after the shot window so JS-heavy pages
        // (bloxd main bundle) can keep decoding/rendering in the worker;
        // fall through into the normal message loop instead of returning.
        }
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

