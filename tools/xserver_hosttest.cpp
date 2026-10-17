// tools/xserver_hosttest.cpp
//
// Standalone host validation of the embedded X11 server wire protocol.
// Simulates the guest client's byte stream (setup handshake -> CreateWindow/
// CreateGC/MapWindow -> Expose -> PolyFillRectangle/PolyLine/ImageText8/
// PutImage) through the same xs_guest_* API elfrun uses, then asserts the
// request/draw counters and framebuffer pixels.
//
// Build (from nefuOS root):
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -I core \
//       core\x11\xserver.cpp core\klib\memory.cpp tools\xserver_hosttest.cpp -o build\xs_hosttest.exe
// Run: build\xs_hosttest.exe   (prints PASS/FAIL, exit 0 on pass)

#include "../core/x11/xserver.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// platform stubs for the server (host side)
namespace nefu {
void* kalloc(size_t n) { return malloc(n); }
void  kfree(void* p) { free(p); }
}

using namespace nefu::x11;

static int g_fail = 0;
static void chk(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static void be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int main() {
    printf("== nefuOS X11 server host test ==\n");
    xs_init();
    xs_dirty_reset();

    // ---- connection ----
    int fd = xs_guest_socket(1, 1);
    chk(fd == 3, "socket -> fd 3");
    chk(xs_guest_connect(fd, 0, 0) == 0, "connect");

    // ---- setup handshake (12 bytes) ----
    uint8_t setup[12] = { 'l', 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0 };
    int w = xs_guest_write(fd, setup, 12);
    chk(w == 12, "setup consumed");
    uint8_t rbuf[1024];
    int n = xs_guest_read(fd, rbuf, sizeof(rbuf));
    chk(n > 0 && rbuf[0] == 1, "setup reply (success)");
    if (n > 0) {
        // parse reply: screens @32, formats @34, then vendor, then screen
        printf("  reply %d B, proto %d.%d, vendor_len=%d\n",
               n, rbuf[2], rbuf[3], (rbuf[42] << 8) | rbuf[43]);
    }

    // ---- req1: CreateWindow + CreateGC + MapWindow (little-endian client) ----
    const uint32_t WIN = 0x2001000, GC = 0x2002000;
    uint8_t req1[80];
    memset(req1, 0, sizeof(req1));
    // CreateWindow (40 B, 10 dwords)
    req1[0] = 1; req1[1] = 24;
    be16(req1 + 2, 10); le16(req1 + 4, 1);
    le32(req1 + 6, WIN); le32(req1 + 10, 0x2000000);
    le16(req1 + 14, 100); le16(req1 + 16, 50);
    le16(req1 + 18, 500); le16(req1 + 20, 400);
    le16(req1 + 22, 0); le16(req1 + 24, 1);
    le32(req1 + 26, 0); le32(req1 + 30, 0x200);      // mask: bit9 = event-mask
    le32(req1 + 34, 0x8045);                          // value: Exposure|KeyPress|ButtonPress|PointerMotion
    // CreateGC (28 B, 7 dwords)
    uint8_t* gc = req1 + 40;
    gc[0] = 55; be16(gc + 2, 7); le16(gc + 4, 1);
    le32(gc + 6, GC); le32(gc + 10, WIN);
    le32(gc + 14, 0xC); le32(gc + 18, 0xFFFFFF); le32(gc + 22, 0);
    // MapWindow (12 B, 3 dwords)
    uint8_t* mw = req1 + 68;
    mw[0] = 8; be16(mw + 2, 3); le16(mw + 4, 1);
    le32(mw + 6, WIN);
    int r1 = xs_guest_write(fd, req1, sizeof(req1));
    chk(r1 == (int)sizeof(req1), "req1 accepted (create/map)");

    // Expose event queued by MapWindow
    n = xs_guest_read(fd, rbuf, sizeof(rbuf));
    chk(n >= 32 && rbuf[0] == 12, "Expose event received");
    if (n >= 32)
        printf("  event type %d, window 0x%x\n", rbuf[0], rbuf[4] | (rbuf[5] << 8) | (rbuf[6] << 16) | (rbuf[7] << 24));

    // ---- req2: draw batch ----
    uint8_t req2[188];
    memset(req2, 0, sizeof(req2));
    // PolyFillRectangle (28 B, 7 dwords): 1 rect @(20,20) 200x150
    uint8_t* fr = req2;
    fr[0] = 70; be16(fr + 2, 7); le16(fr + 4, 1);
    le32(fr + 6, WIN); le32(fr + 10, GC);
    le16(fr + 14, 1);
    le16(fr + 18, 20); le16(fr + 20, 20); le16(fr + 22, 200); le16(fr + 24, 150);
    // PolyLine (36 B, 9 dwords): triangle
    uint8_t* pl = req2 + 28;
    pl[0] = 66; be16(pl + 2, 9); le16(pl + 4, 1);
    le32(pl + 6, WIN); le32(pl + 10, GC);
    pl[14] = 0; pl[15] = 0; le16(pl + 16, 4);
    const int16_t pts[8] = { 250, 180, 350, 60, 450, 180, 250, 180 };
    for (int i = 0; i < 8; i++) le16(pl + 18 + 2 * i, (uint16_t)pts[i]);
    // ImageText8 (32 B, 8 dwords): "nefuOS X11"
    uint8_t* tx = req2 + 64;
    tx[0] = 76; be16(tx + 2, 8); le16(tx + 4, 1);
    le32(tx + 6, WIN); le32(tx + 10, GC);
    le16(tx + 14, 30); le16(tx + 16, 220);
    tx[18] = 10;
    memcpy(tx + 22, "nefuOS X11", 10);
    // PutImage (92 B, 23 dwords): 8x8 ZPixmap @(30,260)
    uint8_t* pi = req2 + 96;
    pi[0] = 72; be16(pi + 2, 23); le16(pi + 4, 1);
    le32(pi + 6, WIN); le32(pi + 10, GC);
    le16(pi + 14, 8); le16(pi + 16, 8);
    le16(pi + 18, 30); le16(pi + 20, 260);
    pi[22] = 0; pi[23] = 24; le16(pi + 24, 2);
    for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) {
            uint8_t* q = pi + 26 + (yy * 8 + xx) * 3;
            q[0] = (xx < 4) ? 255 : 0;      // R
            q[1] = (yy < 4) ? 255 : 0;      // G
            q[2] = (uint8_t)(xx * 32);      // B
        }
    int r2 = xs_guest_write(fd, req2, sizeof(req2));
    chk(r2 == (int)sizeof(req2), "req2 accepted (draw batch)");

    // ---- assertions ----
    printf("  req_count=%u draw_ops=%u windows=%d\n",
           xs_req_count, xs_draw_ops, xs_window_count);
    chk(xs_req_count == 7, "req_count == 7");
    chk(xs_draw_ops >= 70, "draw_ops >= 70");
    chk(xs_window_count == 2, "window_count == 2 (root + client)");

    uint32_t p1 = xs_fb[80 * XS_W + 130];          // inside fill rect -> white
    uint32_t p2 = xs_fb[55 * XS_W + 105];          // window bg, outside rect -> black
    uint32_t p3 = xs_fb[310 * XS_W + 130];         // putimage (0,0) -> yellow
    printf("  pixel rect=0x%06x bg=0x%06x img=0x%06x\n", p1, p2, p3);
    chk(p1 == 0xFFFFFF, "fill rect pixel is white");
    chk(p2 == 0x000000, "window background is black");
    chk(p3 == 0xFFFF00, "putimage pixel is yellow");

    printf("\n== %s ==\n", g_fail ? "HOSTTEST FAIL" : "HOSTTEST PASS");
    return g_fail ? 1 : 0;
}
