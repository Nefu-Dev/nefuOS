// core/x11/xserver.cpp
//
// Embedded X11 protocol server (core subset) — see xserver.h for the design.
// Built for BOTH backends (host + bare).  All state is static and kalloc-free
// (fixed tables), no STL, no exceptions, matching the bare kernel constraints.

#include "xserver.h"
#include "../klib/klib.h"
#include "../platform.h"    // kalloc/kfree

namespace nefu {
namespace x11 {

static void memclr(void* p, size_t n) { nefu::memset(p, 0, n); }

// ===================== state =====================
uint32_t xs_fb[XS_W * XS_H];
int xs_dirty_x0 = XS_W, xs_dirty_y0 = XS_H, xs_dirty_x1 = 0, xs_dirty_y1 = 0;
uint32_t xs_req_count = 0;
uint32_t xs_draw_ops = 0;
int xs_window_count = 0;

static bool xs_le = true;            // client byte order (default little)
static bool xs_attached = false;     // guest connected
static int  xs_fd = -1;              // guest's X connection fd
static uint8_t xs_seq = 0;           // request sequence

static const uint32_t XS_ROOT_ID = 0x2000000;
static const uint32_t XS_RES_ID_BASE = 0x2000000;
static const uint32_t XS_RES_ID_MASK = 0x1FFFFF;

// ---------- windows ----------
struct XWin {
    bool used;
    uint32_t id;
    int x, y, w, h;              // root-relative geometry
    int border;
    bool mapped;
    uint32_t bg;                 // background pixel
    uint32_t event_mask;         // selected events (for delivery filtering)
};
static XWin s_wins[XS_MAX_WINDOWS];

// ---------- GC ----------
struct XGC {
    bool used;
    uint32_t id;
    uint32_t fg, bg;
    int function;                // 0 = clear, 3 = xor, 13 = set
    int line_width;
};
static XGC s_gcs[XS_MAX_GC];

// ---------- pixmaps (off-screen drawables) ----------
struct XPix {
    bool used;
    uint32_t id;
    int w, h;
    uint32_t* data;              // kalloc'd, 0x00RRGGBB
};
static XPix s_pix[XS_MAX_PIXMAPS];

// ---------- event queue (byte stream to the guest) ----------
struct XEventQ { uint8_t* data; uint32_t len, cap, rd; };
static XEventQ s_eq = { 0, 0, 0, 0 };

// ---------- input byte stream from the guest ----------
static uint8_t s_in[4096];
static uint32_t s_in_len = 0;

// ===================== helpers =====================
static void mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= XS_W || y >= XS_H) return;
    if (x + w > XS_W) w = XS_W - x;
    if (y + h > XS_H) h = XS_H - y;
    if (x < xs_dirty_x0) xs_dirty_x0 = x;
    if (y < xs_dirty_y0) xs_dirty_y0 = y;
    if (x + w > xs_dirty_x1) xs_dirty_x1 = x + w;
    if (y + h > xs_dirty_y1) xs_dirty_y1 = y + h;
}

static void fb_put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= XS_W || y >= XS_H) return;
    xs_fb[y * XS_W + x] = c;
}

// draw an axis-aligned line (simple DDA, clipped)
static void draw_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (int i = 0; i < 4096; i++) {
        fb_put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
    mark_dirty(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
               (x0 < x1 ? x1 - x0 : x0 - x1) + 1,
               (y0 < y1 ? y1 - y0 : y0 - y1) + 1);
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            fb_put(x + xx, y + yy, c);
    mark_dirty(x, y, w, h);
}

static XWin* win_find(uint32_t id) {
    for (int i = 0; i < XS_MAX_WINDOWS; i++)
        if (s_wins[i].used && s_wins[i].id == id) return &s_wins[i];
    return 0;
}
static XGC* gc_find(uint32_t id) {
    for (int i = 0; i < XS_MAX_GC; i++)
        if (s_gcs[i].used && s_gcs[i].id == id) return &s_gcs[i];
    return 0;
}
static XPix* pix_find(uint32_t id) {
    for (int i = 0; i < XS_MAX_PIXMAPS; i++)
        if (s_pix[i].used && s_pix[i].id == id) return &s_pix[i];
    return 0;
}

// drawable base: window or pixmap, returns its buffer + base x/y.
// Windows live in the global screen framebuffer -> stride is XS_W.
static uint32_t* draw_base(uint32_t id, int* bx, int* by, int* bw, int* bh) {
    XWin* w = win_find(id);
    if (w) { *bx = w->x; *by = w->y; *bw = XS_W; *bh = XS_H; return xs_fb; }
    XPix* p = pix_find(id);
    if (p) { *bx = 0; *by = 0; *bw = p->w; *bh = p->h; return p->data; }
    return 0;
}

// ---------- byte readers (honor client byte order) ----------
static uint16_t rd16(const uint8_t* p) {
    return xs_le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t rd32(const uint8_t* p) {
    return xs_le ? (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t)(p[3] << 24))
                 : (uint32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}
static void wr16(uint8_t* p, uint16_t v) {
    if (xs_le) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
    else { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
}
static void wr32(uint8_t* p, uint32_t v) {
    if (xs_le) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
    else { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
}

// ===================== event queue =====================
static void evq_push(const uint8_t* ev, uint32_t n) {
    if (s_eq.len + n > s_eq.cap) return;      // drop on overflow (never in practice)
    for (uint32_t i = 0; i < n; i++) s_eq.data[s_eq.len + i] = ev[i];
    s_eq.len += n;
}

static void ev_send_expose(uint32_t wid, int x, int y, int w, int h) {
    uint8_t e[32]; memclr(e, 32);
    e[0] = 12;                       // Expose
    e[2] = 0; e[3] = 0;              // sequence
    wr32(e + 4, wid);
    wr16(e + 8, (uint16_t)x); wr16(e + 10, (uint16_t)y);
    wr16(e + 12, (uint16_t)w); wr16(e + 14, (uint16_t)h);
    wr16(e + 16, 0);                 // count
    evq_push(e, 32);
}

static void ev_send_configure(uint32_t wid, int x, int y, int w, int h) {
    uint8_t e[32]; memclr(e, 32);
    e[0] = 22;                       // ConfigureNotify
    e[1] = 0;
    e[2] = 0; e[3] = 0;
    wr32(e + 4, wid);
    wr32(e + 8, XS_ROOT_ID);
    wr32(e + 12, wid);
    wr16(e + 16, (uint16_t)x); wr16(e + 18, (uint16_t)y);
    wr16(e + 20, (uint16_t)w); wr16(e + 22, (uint16_t)h);
    wr16(e + 24, 0);                 // border
    e[26] = 0;                       // above-sibling: none
    e[27] = 0;                       // override-redirect
    evq_push(e, 32);
}

// ===================== public API =====================
void xs_init() {
    memclr(s_wins, sizeof(s_wins));
    memclr(s_gcs, sizeof(s_gcs));
    memclr(s_pix, sizeof(s_pix));
    s_eq.data = (uint8_t*)nefu::kalloc(XS_EVQ_CAP);
    s_eq.cap = XS_EVQ_CAP; s_eq.len = 0; s_eq.rd = 0;
    xs_attached = false;
    xs_fd = -1;
    xs_req_count = 0; xs_draw_ops = 0;
    xs_window_count = 0;
    xs_le = true;
    xs_seq = 0;
    // root window
    XWin* root = &s_wins[0];
    root->used = true; root->id = XS_ROOT_ID;
    root->x = 0; root->y = 0; root->w = XS_W; root->h = XS_H;
    root->mapped = true; root->bg = 0x000000;
    xs_window_count = 1;
    // clear framebuffer
    for (int i = 0; i < XS_W * XS_H; i++) xs_fb[i] = 0;
    xs_dirty_x0 = 0; xs_dirty_y0 = 0; xs_dirty_x1 = XS_W; xs_dirty_y1 = XS_H;
}

void xs_dirty_reset() {
    xs_dirty_x0 = XS_W; xs_dirty_y0 = XS_H; xs_dirty_x1 = 0; xs_dirty_y1 = 0;
}

bool xs_connected() { return xs_attached; }

// ---------- guest fd plumbing ----------
// forward decls (defined after the request parser)
static uint32_t xs_handle_one(const uint8_t* p, uint32_t len);
int xs_handle(const uint8_t* data, uint32_t len);
void xs_send_setup_reply();

int xs_guest_socket(int domain, int type) {
    (void)type;
    // AF_UNIX(1) or AF_INET(2) with path/port pointing at the X server both map
    // onto the single embedded server connection.
    if (xs_fd >= 0) return -1;                 // one X connection at a time
    xs_fd = 3;                                  // first guest fd
    return xs_fd;
}
int xs_guest_connect(int fd, const void* addr, int alen) {
    (void)addr; (void)alen;
    if (fd != xs_fd) return -1;
    xs_attached = true;
    s_in_len = 0;
    return 0;
}
int xs_guest_write(int fd, const void* buf, int len) {
    if (fd != xs_fd || !xs_attached) return -1;
    const uint8_t* p = (const uint8_t*)buf;
    // setup handshake: first 12 bytes with byte-order char
    if (s_in_len == 0 && len >= 12 && (p[0] == 'l' || p[0] == 'B')) {
        xs_le = (p[0] == 'l');
        // swallow the setup request (12 + auth name/data padding)
        int hdr = 12;
        int n = rd16(p + 4), d = rd16(p + 6);
        int total = hdr + n + d + 3;
        total &= ~3;
        if (total > len) return 0;              // wait for more bytes
        xs_send_setup_reply();                  // handshake reply -> guest read()
        return total;                            // consumed setup
    }
    // request stream
    xs_handle(p, (uint32_t)len);
    return len;
}
int xs_guest_read(int fd, void* buf, int len) {
    if (fd != xs_fd || !xs_attached) return 0;
    uint8_t* out = (uint8_t*)buf;
    int n = 0;
    // hand the guest whatever bytes are queued (Xlib reads are stream-style)
    while (s_eq.rd < s_eq.len && n < len) {
        out[n++] = s_eq.data[s_eq.rd++];
    }
    if (s_eq.rd >= s_eq.len) { s_eq.rd = 0; s_eq.len = 0; }
    return n;
}
int xs_guest_close(int fd) {
    if (fd != xs_fd) return -1;
    xs_fd = -1;
    xs_attached = false;
    return 0;
}

// ---------- host input injection ----------
static void ev_input(int code, int detail, int x, int y, uint32_t state, uint32_t wid) {
    uint8_t e[32]; memclr(e, 32);
    e[0] = (uint8_t)code;
    e[1] = (uint8_t)detail;
    e[2] = 0; e[3] = 0;
    wr32(e + 4, 0);                  // time
    wr32(e + 8, XS_ROOT_ID);         // root
    wr32(e + 12, wid ? wid : XS_ROOT_ID);   // event window
    wr32(e + 16, wid ? wid : XS_ROOT_ID);   // child
    wr16(e + 20, (uint16_t)x); wr16(e + 22, (uint16_t)y);   // root x/y
    wr16(e + 24, (uint16_t)x); wr16(e + 26, (uint16_t)y);   // event x/y
    wr16(e + 28, (uint16_t)state);
    e[30] = 1;                       // same-screen
    e[31] = 0;
    evq_push(e, 32);
}
void xs_inject_key(int keysym, int down) {
    // keysym -> keycode (1:1 for ASCII range; X keycodes are 8 + scancode)
    int kc = keysym < 256 ? (keysym & 0x7F) + 8 : 8;
    ev_input(down ? 2 : 3, kc, 0, 0, 0, XS_ROOT_ID);
}
void xs_inject_pointer(int x, int y, int buttons) {
    uint32_t state = 0;
    if (buttons & 1) state |= 0x100;
    if (buttons & 2) state |= 0x200;
    ev_input(6, 0, x, y, state, XS_ROOT_ID);     // MotionNotify
    if (buttons & 1) ev_input(4, 1, x, y, state, XS_ROOT_ID);   // ButtonPress
    if (buttons & 2) ev_input(5, 1, x, y, state, XS_ROOT_ID);   // ButtonRelease
}

// ===================== request parsing =====================

// Parse one request at p (len >= 4).  Returns bytes consumed, or 0 if more
// data is needed.  Direct replies are appended to s_eq (prefixed as if the
// client's next read).
static uint32_t xs_handle_one(const uint8_t* p, uint32_t len) {
    if (len < 4) return 0;
    uint8_t op = p[0];
    // X11 request header: op(1) | attr(1) | length(2, ALWAYS big-endian) | seq(2)
    uint32_t dwords = ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    uint32_t total = dwords * 4;
    if (total < 4 || total > len) return 0;      // incomplete
    xs_req_count++;

    // window helpers
    auto wid = [&](uint32_t off) -> uint32_t { return rd32(p + off); };

    switch (op) {
    case 1: {   // CreateWindow (wid@6, parent@10, geom @14-21, class@24, mask@30, values@34)
        if (total < 34) break;
        int depth = p[1];
        (void)depth;
        uint32_t id = wid(6);
        uint32_t parent = wid(10);
        int x = (int)rd16(p + 14), y = (int)rd16(p + 16);
        int w = (int)rd16(p + 18), h = (int)rd16(p + 20);
        int bw = (int)rd16(p + 22);
        uint32_t cls = rd16(p + 24);
        uint32_t mask = rd32(p + 30);
        (void)parent; (void)cls;
        XWin* wn = 0;
        for (int i = 1; i < XS_MAX_WINDOWS; i++)
            if (!s_wins[i].used) { wn = &s_wins[i]; break; }
        if (!wn) break;
        uint32_t vofs = 34;
        uint32_t bg = 0;
        uint32_t emask = 0;
        if (mask & (1u << 0))  { if (vofs + 4 <= total) bg = rd32(p + vofs); vofs += 4; }
        if (mask & (1u << 1))  vofs += 4;
        if (mask & (1u << 2))  vofs += 1;
        if (mask & (1u << 3))  vofs += 1;
        if (mask & (1u << 4))  vofs += 1;
        if (mask & (1u << 5))  vofs += 4;
        if (mask & (1u << 6))  vofs += 4;
        if (mask & (1u << 7))  vofs += 1;
        if (mask & (1u << 8))  vofs += 1;
        if (mask & (1u << 9))  { if (vofs + 4 <= total) emask = rd32(p + vofs); vofs += 4; }
        if (mask & (1u << 10)) vofs += 4;
        if (mask & (1u << 11)) vofs += 4;
        if (mask & (1u << 12)) vofs += 4;
        wn->used = true; wn->id = id;
        wn->x = x; wn->y = y; wn->w = w < 1 ? 1 : w; wn->h = h < 1 ? 1 : h;
        wn->border = bw; wn->mapped = false; wn->bg = bg; wn->event_mask = emask;
        xs_window_count++;
        break;
    }
    case 2: {   // ChangeWindowAttributes (wid@6, mask@10, values@12)
        uint32_t id = wid(6);
        uint32_t mask = rd32(p + 10);
        uint32_t vofs = 12;
        XWin* wn = win_find(id);
        if (!wn) break;
        if (mask & (1u << 0))  { if (vofs + 4 <= total) wn->bg = rd32(p + vofs); vofs += 4; }
        if (mask & (1u << 1))  vofs += 4;
        if (mask & (1u << 2))  vofs += 1;
        if (mask & (1u << 3))  vofs += 1;
        if (mask & (1u << 4))  vofs += 1;
        if (mask & (1u << 5))  vofs += 4;
        if (mask & (1u << 6))  vofs += 4;
        if (mask & (1u << 7))  vofs += 1;
        if (mask & (1u << 8))  vofs += 1;
        if (mask & (1u << 9))  { if (vofs + 4 <= total) wn->event_mask = rd32(p + vofs); vofs += 4; }
        if (mask & (1u << 10)) vofs += 4;
        if (mask & (1u << 11)) vofs += 4;
        if (mask & (1u << 12)) vofs += 4;
        break;
    }
    case 3: {   // GetWindowAttributes (wid@6)
        uint32_t id = wid(6);
        XWin* wn = win_find(id);
        uint8_t r[44]; memclr(r, 44);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 3);                 // extra length 3 (12 more bytes)
        wr32(r + 8, wn ? (uint32_t)(wn->mapped ? 0 : 2) : 0);  // map state
        wr32(r + 12, XS_ROOT_ID);       // root
        wr32(r + 16, wn ? (uint32_t)wn->event_mask : 0);
        r[20] = 0;                      // bit gravity
        r[21] = 0;                      // win gravity
        r[22] = 0;                      // backing store
        r[23] = 0;
        wr32(r + 24, 0);                // backing planes
        wr32(r + 28, 0);                // backing pixel
        r[32] = 0;                      // override redirect
        r[33] = 0;                      // colormap
        r[34] = 0;
        r[35] = 1;                      // cursor
        wr32(r + 36, XS_RES_ID_BASE);   // visual
        wr16(r + 40, 3);                // class InputOutput
        wr16(r + 42, 24);               // depth
        evq_push(r, 44);
        break;
    }
    case 4: {   // DestroyWindow (wid@6)
        uint32_t id = wid(6);
        for (int i = 0; i < XS_MAX_WINDOWS; i++)
            if (s_wins[i].used && s_wins[i].id == id) {
                s_wins[i].used = false;
                if (i == 0) s_wins[0].used = true;    // never free root
                xs_window_count--;
                break;
            }
        break;
    }
    case 8: {   // MapWindow (wid@6)
        uint32_t id = wid(6);
        XWin* wn = win_find(id);
        if (!wn) break;
        wn->mapped = true;
        // background paint
        fill_rect(wn->x, wn->y, wn->w, wn->h, wn->bg);
        ev_send_expose(id, 0, 0, wn->w, wn->h);
        break;
    }
    case 10: {  // UnmapWindow (wid@6)
        uint32_t id = wid(6);
        XWin* wn = win_find(id);
        if (wn) wn->mapped = false;
        break;
    }
    case 12: {  // ConfigureWindow (wid@6, mask@10, values@12)
        uint32_t id = wid(6);
        uint32_t mask = rd16(p + 10);
        uint32_t vofs = 12;
        XWin* wn = win_find(id);
        if (!wn) break;
        if (mask & 0x1) { if (vofs + 4 <= total) wn->x = (int)rd32(p + vofs); vofs += 4; }
        if (mask & 0x2) { if (vofs + 4 <= total) wn->y = (int)rd32(p + vofs); vofs += 4; }
        if (mask & 0x4) { if (vofs + 4 <= total) wn->w = (int)rd32(p + vofs); vofs += 4; }
        if (mask & 0x8) { if (vofs + 4 <= total) wn->h = (int)rd32(p + vofs); vofs += 4; }
        if (mask & 0x10) vofs += 2;
        if (mask & 0x20) vofs += 1;
        if (mask & 0x40) vofs += 4;
        ev_send_configure(id, wn->x, wn->y, wn->w, wn->h);
        if (wn->mapped) ev_send_expose(id, 0, 0, wn->w, wn->h);
        break;
    }
    case 14: {  // GetGeometry (wid@6)
        uint32_t id = wid(6);
        XWin* wn = win_find(id);
        XPix* px = pix_find(id);
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 24;            // depth
        r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);                 // extra len
        wr32(r + 8, XS_ROOT_ID);        // root
        wr16(r + 12, (uint16_t)(wn ? wn->x : (px ? 0 : 0)));
        wr16(r + 14, (uint16_t)(wn ? wn->y : 0));
        wr16(r + 16, (uint16_t)(wn ? wn->w : (px ? px->w : 0)));
        wr16(r + 18, (uint16_t)(wn ? wn->h : (px ? px->h : 0)));
        wr16(r + 20, (uint16_t)(wn ? wn->border : 0));
        evq_push(r, 32);
        break;
    }
    case 15: {  // QueryTree (wid@6)
        uint32_t id = wid(6);
        (void)id;
        uint8_t r[36]; memclr(r, 36);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 1);                 // extra 4 bytes (1 child id)
        wr32(r + 8, XS_ROOT_ID);        // root
        wr32(r + 12, XS_ROOT_ID);       // parent
        wr16(r + 16, 0);                // children count
        evq_push(r, 36);
        break;
    }
    case 16: {  // InternAtom (nlen@6, name@12)
        uint32_t nlen = rd16(p + 6);
        // atom = simple hash of the name (stable per process)
        uint32_t h = 0x811C9DC5u;
        for (uint32_t i = 0; i + 12 < total && i < nlen; i++) {
            h ^= p[12 + i]; h *= 0x01000193u;
        }
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);
        wr32(r + 8, h & 0x00FFFFFFu);
        evq_push(r, 32);
        break;
    }
    case 18: {  // ChangeProperty (accepted, no storage)
        break;
    }
    case 53: {  // CreatePixmap (pid@6, drawable@10, w@14, h@16)
        int depth = p[1] == 1 ? 0 : (int)p[1];
        uint32_t id = wid(6);
        uint32_t draw = wid(10);
        int w = (int)rd16(p + 14), h = (int)rd16(p + 16);
        (void)depth; (void)draw;
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) break;
        XPix* px = 0;
        for (int i = 0; i < XS_MAX_PIXMAPS; i++)
            if (!s_pix[i].used) { px = &s_pix[i]; break; }
        if (!px) break;
        uint32_t* d = (uint32_t*)nefu::kalloc((uint32_t)w * (uint32_t)h * 4);
        if (!d) break;
        px->used = true; px->id = id; px->w = w; px->h = h; px->data = d;
        break;
    }
    case 54: {  // FreePixmap (pid@6)
        uint32_t id = wid(6);
        for (int i = 0; i < XS_MAX_PIXMAPS; i++)
            if (s_pix[i].used && s_pix[i].id == id) {
                if (s_pix[i].data) nefu::kfree(s_pix[i].data);
                s_pix[i].used = false;
                break;
            }
        break;
    }
    case 55: {  // CreateGC (cid@6, drawable@10, mask@14, values@18)
        uint32_t cid = wid(6);
        uint32_t mask = rd32(p + 14);
        uint32_t vofs = 18;
        uint32_t fg = 0, bg = 0;
        if (mask & 0x4)  { if (vofs + 4 <= total) fg = rd32(p + vofs); vofs += 4; }
        if (mask & 0x8)  { if (vofs + 4 <= total) bg = rd32(p + vofs); vofs += 4; }
        XGC* g = 0;
        for (int i = 0; i < XS_MAX_GC; i++)
            if (!s_gcs[i].used) { g = &s_gcs[i]; break; }
        if (!g) break;
        g->used = true; g->id = cid; g->fg = fg; g->bg = bg;
        g->function = 13; g->line_width = 0;
        // skip all value fields (order per X protocol)
        if (mask & 0x1) vofs += 1;          // function
        if (mask & 0x2) vofs += 4;          // plane mask
        if (mask & 0x4) vofs += 4;          // fg (already read)
        if (mask & 0x8) vofs += 4;          // bg (already read)
        if (mask & 0x10) vofs += 2;         // line width
        if (mask & 0x20) vofs += 1;
        if (mask & 0x40) vofs += 1;
        if (mask & 0x80) vofs += 1;
        if (mask & 0x100) vofs += 1;
        if (mask & 0x200) vofs += 1;
        if (mask & 0x400) vofs += 4;
        if (mask & 0x800) vofs += 4;
        if (mask & 0x1000) vofs += 4;
        if (mask & 0x2000) vofs += 4;
        if (mask & 0x4000) vofs += 4;
        if (mask & 0x8000) vofs += 1;
        if (mask & 0x10000) vofs += 1;
        if (mask & 0x20000) vofs += 1;
        if (mask & 0x40000) vofs += 1;
        if (mask & 0x80000) vofs += 2;
        if (mask & 0x100000) vofs += 2;
        if (mask & 0x200000) vofs += 1;
        break;
    }
    case 56: {  // ChangeGC (cid@6, mask@10, values@12)
        uint32_t cid = wid(6);
        uint32_t mask = rd32(p + 10);
        uint32_t vofs = 12;
        XGC* g = gc_find(cid);
        if (!g) break;
        if (mask & 0x1)  { if (vofs < total) g->function = p[vofs]; vofs += 1; }
        if (mask & 0x2)  vofs += 4;
        if (mask & 0x4)  { if (vofs + 4 <= total) g->fg = rd32(p + vofs); vofs += 4; }
        if (mask & 0x8)  { if (vofs + 4 <= total) g->bg = rd32(p + vofs); vofs += 4; }
        if (mask & 0x10) { if (vofs + 2 <= total) g->line_width = rd16(p + vofs); vofs += 2; }
        vofs += (uint32_t)((__builtin_popcount(mask & ~0x1Fu)) * 4 + (__builtin_popcount(mask & 0x1Fu)) * 1);
        (void)vofs;
        break;
    }
    case 60: {  // FreeGC (cid@6)
        uint32_t cid = wid(6);
        for (int i = 0; i < XS_MAX_GC; i++)
            if (s_gcs[i].used && s_gcs[i].id == cid) { s_gcs[i].used = false; break; }
        break;
    }
    case 61: {  // ClearArea (wid@6, x@10, y@12, w@14, h@16)
        uint32_t id = wid(6);
        int x = (int)rd16(p + 10), y = (int)rd16(p + 12);
        int w = (int)rd16(p + 14), h = (int)rd16(p + 16);
        XWin* wn = win_find(id);
        uint32_t bg = wn ? wn->bg : 0;
        if (w == 0) w = wn ? wn->w - x : XS_W;
        if (h == 0) h = wn ? wn->h - y : XS_H;
        if (wn) { fill_rect(wn->x + x, wn->y + y, w, h, bg); }
        break;
    }
    case 62: {  // CopyArea (src@6, dst@10, gc@14, sx@18, sy@20, w@22, h@24, dx@26, dy@28)
        uint32_t src = wid(6), dst = wid(10);
        uint32_t gc = wid(14);
        int sx = (int)rd16(p + 18), sy = (int)rd16(p + 20);
        int w = (int)rd16(p + 22), h = (int)rd16(p + 24);
        int dx = (int)rd16(p + 26), dy = (int)rd16(p + 28);
        (void)gc;
        int sbx, sby, sbw, sbh, dbx, dby, dbw, dbh;
        uint32_t* s = draw_base(src, &sbx, &sby, &sbw, &sbh);
        uint32_t* d = draw_base(dst, &dbx, &dby, &dbw, &dbh);
        if (!s || !d) break;
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                int spx = sx + xx, spy = sy + yy;
                int dpx = dx + xx, dpy = dy + yy;
                if (spx < 0 || spy < 0 || spx >= sbw || spy >= sbh) continue;
                if (dpx < 0 || dpy < 0 || dpx >= dbw || dpy >= dbh) continue;
                d[(dby + dpy) * dbw + (dbx + dpx)] = s[(sby + spy) * sbw + (sbx + spx)];
            }
        mark_dirty(dbx + dx, dby + dy, w, h);
        xs_draw_ops++;
        break;
    }
    case 65: {  // PolyPoint (d@6, g@10, n@14, points@16)
        uint32_t d = wid(6), g = wid(10);
        uint32_t n = rd16(p + 14);
        uint32_t vofs = 16;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        uint32_t c = gc ? gc->fg : 0;
        if (!base) break;
        for (uint32_t i = 0; i < n && vofs + 4 <= total; i++) {
            int x = (int)rd16(p + vofs), y = (int)rd16(p + vofs + 2);
            vofs += 4;
            if (x < 0 || y < 0 || x >= bw || y >= bh) continue;
            base[(by + y) * bw + (bx + x)] = c;
            if (base == xs_fb) mark_dirty(bx + x, by + y, 1, 1);
        }
        xs_draw_ops += n;
        break;
    }
    case 66: {  // PolyLine (d@6, g@10, mode@14, n@16, points@18)
        uint32_t d = wid(6), g = wid(10);
        uint32_t n = rd16(p + 16);
        uint32_t vofs = 18;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        uint32_t c = gc ? gc->fg : 0;
        if (!base) break;
        if (n == 0) break;
        int px = 0, py = 0;
        for (uint32_t i = 0; i < n && vofs + 4 <= total; i++) {
            int x = (int)rd16(p + vofs), y = (int)rd16(p + vofs + 2);
            vofs += 4;
            if (i > 0 && base == xs_fb)
                draw_line(bx + px, by + py, bx + x, by + y, c);
            px = x; py = y;
        }
        xs_draw_ops += n;
        break;
    }
    case 67: {  // PolySegment (d@6, g@10, n@16, segs@18)
        uint32_t d = wid(6), g = wid(10);
        uint32_t n = rd16(p + 16);
        uint32_t vofs = 18;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        uint32_t c = gc ? gc->fg : 0;
        if (!base) break;
        for (uint32_t i = 0; i < n && vofs + 8 <= total; i++) {
            int x1 = (int)rd16(p + vofs), y1 = (int)rd16(p + vofs + 2);
            int x2 = (int)rd16(p + vofs + 4), y2 = (int)rd16(p + vofs + 6);
            vofs += 8;
            if (base == xs_fb) draw_line(bx + x1, by + y1, bx + x2, by + y2, c);
            else {
                for (int yy = y1 > y2 ? y2 : y1; yy <= (y1 > y2 ? y1 : y2) && yy < bh && yy >= 0; yy++)
                    for (int xx = x1 > x2 ? x2 : x1; xx <= (x1 > x2 ? x1 : x2) && xx < bw && xx >= 0; xx++)
                        base[(by + yy) * bw + (bx + xx)] = c;
            }
        }
        xs_draw_ops += n;
        break;
    }
    case 69: {  // PolyRectangle (d@6, g@10, n@16, rects@18)
        uint32_t d = wid(6), g = wid(10);
        uint32_t n = rd16(p + 16);
        uint32_t vofs = 18;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        uint32_t c = gc ? gc->fg : 0;
        if (!base) break;
        for (uint32_t i = 0; i < n && vofs + 8 <= total; i++) {
            int x = (int)rd16(p + vofs), y = (int)rd16(p + vofs + 2);
            int w = (int)rd16(p + vofs + 4), h = (int)rd16(p + vofs + 6);
            vofs += 8;
            if (base == xs_fb) {
                draw_line(bx + x, by + y, bx + x + w - 1, by + y, c);
                draw_line(bx + x, by + y + h - 1, bx + x + w - 1, by + y + h - 1, c);
                draw_line(bx + x, by + y, bx + x, by + y + h - 1, c);
                draw_line(bx + x + w - 1, by + y, bx + x + w - 1, by + y + h - 1, c);
            }
        }
        xs_draw_ops += n;
        break;
    }
    case 70: {  // PolyFillRectangle (d@6, g@10, n@14, pad@16, rects@18)
        uint32_t d = wid(6), g = wid(10);
        uint32_t n = rd16(p + 14);
        uint32_t vofs = 18;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        uint32_t c = gc ? gc->fg : 0;
        if (!base) break;
        for (uint32_t i = 0; i < n && vofs + 8 <= total; i++) {
            int x = (int)rd16(p + vofs), y = (int)rd16(p + vofs + 2);
            int w = (int)rd16(p + vofs + 4), h = (int)rd16(p + vofs + 6);
            vofs += 8;
            if (base == xs_fb) { fill_rect(bx + x, by + y, w, h, c); }
            else {
                for (int yy = 0; yy < h && by + y + yy < bh; yy++)
                    for (int xx = 0; xx < w && bx + x + xx < bw; xx++)
                        base[(by + y + yy) * bw + (bx + x + xx)] = c;
            }
        }
        xs_draw_ops += n;
        break;
    }
    case 72: {  // PutImage (d@6, g@10, w@14, h@16, dx@18, dy@20, data@26)
        uint32_t d = wid(6), g = wid(10);
        int w = (int)rd16(p + 14), h = (int)rd16(p + 16);
        int dx = (int)rd16(p + 18), dy = (int)rd16(p + 20);
        // left_pad @22 (1), depth @23, format @24 (2 == ZPixmap)
        uint32_t vofs = 26;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        (void)gc;
        if (!base || w <= 0 || h <= 0) break;
        int rowpad = (4 - (w * 3) % 4) % 4;   // 24bpp rows are 32-bit aligned
        if (xs_le) {
            for (int yy = 0; yy < h; yy++) {
                uint32_t rowoff = vofs + (uint32_t)yy * (uint32_t)(w * 3 + rowpad);
                for (int xx = 0; xx < w; xx++) {
                    uint32_t o = rowoff + (uint32_t)xx * 3;
                    uint32_t c = ((uint32_t)p[o] << 16) | ((uint32_t)p[o + 1] << 8) | p[o + 2];
                    int fx = bx + dx + xx, fy = by + dy + yy;
                    if (fx < 0 || fy < 0 || fx >= bw || fy >= bh) continue;
                    base[fy * bw + fx] = c;
                }
            }
        } else {
            for (int yy = 0; yy < h; yy++) {
                uint32_t rowoff = vofs + (uint32_t)yy * (uint32_t)(w * 3 + rowpad);
                for (int xx = 0; xx < w; xx++) {
                    uint32_t o = rowoff + (uint32_t)xx * 3;
                    uint32_t c = ((uint32_t)p[o] << 16) | ((uint32_t)p[o + 1] << 8) | p[o + 2];
                    int fx = bx + dx + xx, fy = by + dy + yy;
                    if (fx < 0 || fy < 0 || fx >= bw || fy >= bh) continue;
                    base[fy * bw + fx] = c;
                }
            }
        }
        mark_dirty(bx + dx, by + dy, w, h);
        xs_draw_ops += (uint32_t)(w * h);
        break;
    }
    case 73: {  // GetImage (d@6, x@10, y@12, w@14, h@16)
        uint32_t d = wid(6);
        int x = (int)rd16(p + 10), y = (int)rd16(p + 12);
        int w = (int)rd16(p + 14), h = (int)rd16(p + 16);
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        if (!base || w <= 0 || h <= 0) break;
        int rowpad = (4 - (w * 3) % 4) % 4;
        uint32_t body = (uint32_t)(w * 3 + rowpad) * (uint32_t)h;
        uint32_t ext = (body + 3) / 4;
        if (32 + body > 4096) break;
        uint8_t r[4096]; memclr(r, 4096);
        r[0] = 1; r[1] = 24;            // depth
        r[2] = 0; r[3] = 0;
        wr32(r + 4, ext);
        wr32(r + 8, 2);                 // ZPixmap
        uint32_t o = 32;
        for (int yy = 0; yy < h; yy++) {
            uint32_t rowoff = o;
            for (int xx = 0; xx < w; xx++) {
                int sx = bx + x + xx, sy = by + y + yy;
                uint32_t c = (sx < 0 || sy < 0 || sx >= bw || sy >= bh) ? 0 : base[sy * bw + sx];
                r[rowoff + (uint32_t)xx * 3] = (uint8_t)((c >> 16) & 0xFF);
                r[rowoff + (uint32_t)xx * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
                r[rowoff + (uint32_t)xx * 3 + 2] = (uint8_t)(c & 0xFF);
            }
            o = rowoff + (uint32_t)(w * 3 + rowpad);
        }
        evq_push(r, 32 + body);
        break;
    }
    case 74: case 76: {  // PolyText8 / ImageText8 (d@6, g@10, x@14, y@16, len@18)
        uint32_t d = wid(6), g = wid(10);
        int x = (int)rd16(p + 14), y = (int)rd16(p + 16);
        uint32_t n = (op == 76) ? p[18] : 0;
        (void)n;
        int bx, by, bw, bh;
        uint32_t* base = draw_base(d, &bx, &by, &bw, &bh);
        XGC* gc = gc_find(g);
        if (base == xs_fb && gc)
            fill_rect(bx + x, by + y - 10, 8 * 8, 12, gc->bg);  // crude text slot
        xs_draw_ops++;
        break;
    }
    case 78: {  // CreateColormap (cmap@6)
        uint32_t cmap = wid(6);
        (void)cmap;
        break;
    }
    case 79: {  // FreeColormap
        break;
    }
    case 84: {  // AllocColor (cmap@6, red@10, green@12, blue@14)
        uint32_t cmap = wid(6);
        (void)cmap;
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);
        uint32_t red = rd16(p + 10), grn = rd16(p + 12), blu = rd16(p + 14);
        uint32_t pixel = ((red >> 8) << 16) | ((grn >> 8) << 8) | (blu >> 8);
        wr32(r + 8, pixel);
        wr16(r + 12, red); wr16(r + 14, grn); wr16(r + 16, blu);
        evq_push(r, 32);
        break;
    }
    case 98: {  // QueryExtension -> not supported
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);
        evq_push(r, 32);
        break;
    }
    case 99: {  // ListExtensions
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);
        evq_push(r, 32);
        break;
    }
    case 101: { // GetKeyboardMapping (first_keycode@6, count@8)
        uint32_t n = rd16(p + 8);      // count
        uint32_t per = 1;
        uint8_t r[32 + 255 * 4]; memclr(r, sizeof(r));
        r[0] = 1; r[1] = (uint8_t)per;
        r[2] = 0; r[3] = 0;
        wr32(r + 4, n);
        uint32_t o = 32;
        for (uint32_t i = 0; i < n && o + 4 <= sizeof(r); i++) {
            wr32(r + o, (uint32_t)(8 + i)); o += 4;
        }
        evq_push(r, 32 + n * 4);
        break;
    }
    case 103: { // GetFontPath
        uint8_t r[32]; memclr(r, 32);
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 0;
        wr32(r + 4, 0);
        evq_push(r, 32);
        break;
    }
    case 108: { // SetInputFocus
        break;
    }
    default:
        // Unknown request: still consumed (guest may query extensions etc.)
        break;
    }
    return total;
}

// Feed a request byte stream; replies/events accumulate in the event queue.
int xs_handle(const uint8_t* data, uint32_t len) {
    uint32_t pos = 0;
    while (pos + 4 <= len) {
        uint32_t used = xs_handle_one(data + pos, len - pos);
        if (used == 0) break;            // need more data
        pos += used;
    }
    return (int)pos;
}

// Reply/event bytes for the guest's read().
int xs_poll(uint8_t* out, int cap) {
    int n = 0;
    while (s_eq.rd < s_eq.len && n < cap) {
        out[n++] = s_eq.data[s_eq.rd++];
    }
    if (s_eq.rd >= s_eq.len) { s_eq.rd = 0; s_eq.len = 0; }
    return n;
}

// ===================== setup reply =====================
// Called by xs_guest_write() after it consumes the setup request; appends the
// handshake reply so the guest's first read() gets it.  Returned via s_eq.
void xs_send_setup_reply() {
    uint8_t r[512]; memclr(r, 512);
    // header (always big-endian per X spec)
    r[0] = 1;                    // success
    r[1] = 0;
    r[2] = 11; r[3] = 0;         // protocol 11.0
    r[4] = 0; r[5] = 0;          // auth data length
    r[6] = 0x0A; r[7] = 0x7E; r[8] = 0x00; r[9] = 0x00;   // release
    r[10] = 0x02; r[11] = 0x00; r[12] = 0x00; r[13] = 0x00;  // resource id base
    r[14] = 0x00; r[15] = 0x1F; r[16] = 0xFF; r[17] = 0xFF;  // mask
    r[18] = 0; r[19] = 0; r[20] = 0; r[21] = 0;   // motion buffer
    r[22] = 0x00; r[23] = 0x00; r[24] = 0x10; r[25] = 0x00; // max request len
    r[26] = 0; r[27] = 0; r[28] = 0; r[29] = 0;   // rotation
    r[30] = 0; r[31] = 0; r[32] = 0; r[33] = 1;   // screens = 1
    r[34] = 3;                   // number of formats
    r[35] = 0;                   // image byte order: LSB first
    r[36] = 32;                  // bitmap scanline unit
    r[37] = 32;                  // scanline pad
    r[38] = 0;                   // bitmap byte order: LSB
    r[39] = 8;                   // min keycode
    r[40] = 255;                 // max keycode
    r[41] = 24;                  // root depth
    // vendor name: len (2) + "nefuOS" (6) + pad
    uint32_t o = 42;
    r[o] = 0; r[o + 1] = 6; o += 2;
    static const char* VN = "nefuOS";
    for (int i = 0; i < 6; i++) r[o++] = (uint8_t)VN[i];
    // pad to 4
    while (o & 3) r[o++] = 0;
    // formats: 24bpp, 32bpp, 16bpp (8 bytes each)
    for (int f = 0; f < 3; f++) {
        r[o++] = 24; r[o++] = 24; r[o++] = 32;   // depth, bits, scanline
        r[o++] = 0;                              // pad
    }
    // screen (40 bytes)
    uint32_t so = o;
    r[so] = 0x02; r[so + 1] = 0x00; r[so + 2] = 0x00; r[so + 3] = 0x00;   // root window
    r[so + 4] = 0x02; r[so + 5] = 0x00; r[so + 6] = 0x00; r[so + 7] = 0x00; // colormap
    r[so + 8] = 0xFF; r[so + 9] = 0xFF; r[so + 10] = 0xFF; r[so + 11] = 0xFF; // white
    r[so + 12] = 0; r[so + 13] = 0; r[so + 14] = 0; r[so + 15] = 0;         // black
    r[so + 16] = 0; r[so + 17] = 0; r[so + 18] = 0; r[so + 19] = 0;         // input masks
    r[so + 20] = 0x04; r[so + 21] = 0x00;   // width 1024
    r[so + 22] = 0x03; r[so + 23] = 0x00;   // height 768
    r[so + 24] = 0x01; r[so + 25] = 0x00;   // min width
    r[so + 26] = 0x01; r[so + 27] = 0x00;   // min height
    r[so + 28] = 0x01; r[so + 29] = 0x00; r[so + 30] = 0x00; r[so + 31] = 0x00; // root visual
    r[so + 32] = 0; r[so + 33] = 0; r[so + 34] = 0; r[so + 35] = 0;          // backing
    r[so + 36] = 0; r[so + 37] = 0;   // save unders
    r[so + 38] = 24; r[so + 39] = 0;  // root depth
    // visual (one, depth 24)
    o = so + 40;
    r[o] = 0x01; r[o + 1] = 0x00; r[o + 2] = 0x00; r[o + 3] = 0x00;   // visual id
    r[o + 4] = 0x04; r[o + 5] = 0x00;   // class TrueColor
    r[o + 6] = 0x18; r[o + 7] = 0x00;   // bits per rgb 24
    r[o + 8] = 0x00; r[o + 9] = 0x01; r[o + 10] = 0x00; r[o + 11] = 0x00;  // colormap entries 256
    r[o + 12] = 0x00; r[o + 13] = 0xFF; r[o + 14] = 0x00; r[o + 15] = 0x00;  // red mask
    r[o + 16] = 0x00; r[o + 17] = 0x00; r[o + 18] = 0xFF; r[o + 19] = 0x00;  // green mask
    r[o + 20] = 0x00; r[o + 21] = 0x00; r[o + 22] = 0x00; r[o + 23] = 0xFF;  // blue mask
    r[o + 24] = 16; r[o + 25] = 0;   // red shift
    r[o + 26] = 8;  r[o + 27] = 0;   // green shift
    r[o + 28] = 0;  r[o + 29] = 0;   // blue shift
    o += 32;
    evq_push(r, o);
}

} // namespace x11
} // namespace nefu
