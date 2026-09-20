// nefuOS Image Viewer - LVGL GUI
// Decodes BMP/PPM natively; on the Win32 host the platform decoder (GDI+)
// plus stb_image handle JPEG/PNG/GIF so real-world images open too.
#include "apps.h"
#include "jpeg.h"
#include "../gui/wm.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct ImageViewState {
    List<FSNode*> files;
    int index;
    int zoom;
    Surface src;
    Surface scaled;
    bool dirty;
    bool src_ok;
    int w, h;
};

static const char* PIC_DIR = "/home/user/Pictures";

static bool is_image_ext(const char* n, int len) {
    if (len < 4) return false;
    const char* e = n + len - 4;
    return strcmp(e, ".ppm") == 0 || strcmp(e, ".pbm") == 0 || strcmp(e, ".bmp") == 0 ||
           strcmp(e, ".jpg") == 0 || strcmp(e, ".png") == 0 || strcmp(e, ".gif") == 0;
}
static bool is_image_ext3(const char* n, int len) {
    if (len < 4) return false;
    return strcmp(n + len - 3, "jpg") == 0 || strcmp(n + len - 3, "img") == 0 ||
           strcmp(n + len - 3, "png") == 0 || strcmp(n + len - 3, "gif") == 0;
}

static bool ppm_decode(const uint8_t* data, uint32_t size, Surface& out) {
    uint32_t i = 0;
    if (size < 4 || data[0] != 'P' || data[1] != '6') return false;
    i = 2;
    int w = 0, h = 0, maxv = 0;
    auto skip_ws = [&]() {
        while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r')) i++;
    };
    auto read_num = [&]() -> int {
        int v = 0;
        while (i < size && data[i] >= '0' && data[i] <= '9') { v = v * 10 + (data[i] - '0'); i++; }
        return v;
    };
    skip_ws(); w = read_num();
    skip_ws(); h = read_num();
    skip_ws(); maxv = read_num();
    skip_ws();
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || maxv == 0) return false;
    if (i + (uint32_t)w * (uint32_t)h * 3 > size) return false;
    out.addr = (uint8_t*)kalloc((size_t)w * (size_t)h * 4);
    if (!out.addr) return false;
    out.width = w; out.height = h; out.pitch = w * 4;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
            i += 3;
            out.setpx(x, y, c);
        }
    }
    return true;
}

static bool bmp_decode(const uint8_t* data, uint32_t size, Surface& out) {
    if (size < 54 || data[0] != 'B' || data[1] != 'M') return false;
    uint32_t off = (uint32_t)data[10] | ((uint32_t)data[11] << 8) |
                   ((uint32_t)data[12] << 16) | ((uint32_t)data[13] << 24);
    int32_t w = (int32_t)(data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24));
    int32_t h = (int32_t)(data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24));
    uint16_t bpp = (uint16_t)(data[28] | (data[29] << 8));
    uint32_t comp = (uint32_t)data[30] | ((uint32_t)data[31] << 8) |
                    ((uint32_t)data[32] << 16) | ((uint32_t)data[33] << 24);
    if (w <= 0 || h == 0 || w > 2048 || h > 2048 || h < -2048) return false;
    if (bpp != 24 && bpp != 32) return false;
    if (comp != 0) return false;
    bool bottom_up = h > 0;
    int ah = h < 0 ? -h : h;
    if (off + (uint32_t)w * (uint32_t)ah * 4 > size) return false;
    out.addr = (uint8_t*)kalloc((size_t)w * (size_t)ah * 4);
    if (!out.addr) return false;
    out.width = w; out.height = ah; out.pitch = w * 4;
    uint32_t rowbytes = ((uint32_t)w * bpp / 8 + 3u) & ~3u;
    for (int y = 0; y < ah; y++) {
        int sy = bottom_up ? (ah - 1 - y) : y;
        const uint8_t* row = data + off + (uint32_t)sy * rowbytes;
        for (int x = 0; x < w; x++) {
            uint32_t c;
            if (bpp == 24) {
                c = ((uint32_t)row[x * 3 + 2] << 16) | ((uint32_t)row[x * 3 + 1] << 8) | row[x * 3];
            } else {
                uint32_t b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];
                c = (r << 16) | (g << 8) | b;
            }
            out.setpx(x, y, c);
        }
    }
    return true;
}

static void image_recalc_scale(ImageViewState* st, int avail_w, int avail_h) {
    if (st->scaled.addr) { kfree(st->scaled.addr); st->scaled.addr = 0; }
    if (!st->src_ok) return;
    int iw = st->src.width, ih = st->src.height;
    int dw = iw, dh = ih;
    if (st->zoom <= 0) {
        if (iw > avail_w || ih > avail_h) {
            int rw = avail_w * 1024 / iw;
            int rh = avail_h * 1024 / ih;
            int r = rw < rh ? rw : rh;
            if (r < 1) r = 1;
            dw = iw * r / 1024;
            dh = ih * r / 1024;
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;
        }
    } else {
        dw = iw * st->zoom / 100;
        dh = ih * st->zoom / 100;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    st->scaled.addr = (uint8_t*)kalloc((size_t)dw * (size_t)dh * 4);
    if (!st->scaled.addr) return;
    st->scaled.width = dw; st->scaled.height = dh; st->scaled.pitch = dw * 4;
    for (int y = 0; y < dh; y++) {
        int sy = (y * ih) / dh;
        const uint32_t* srow = (const uint32_t*)(st->src.addr + (size_t)sy * st->src.pitch);
        uint32_t* drow = (uint32_t*)(st->scaled.addr + (size_t)y * st->scaled.pitch);
        for (int x = 0; x < dw; x++) {
            int sx = (x * iw) / dw;
            drow[x] = srow[sx];
        }
    }
    st->dirty = false;
}

bool stbi_decode_mem(const uint8_t* data, int len, int* w, int* h, uint8_t** out);
static bool stbi_decode(const uint8_t* data, uint32_t size, Surface& out) {
    int w = 0, h = 0;
    uint8_t* buf = 0;
    if (!nefu::stbi_decode_mem(data, (int)size, &w, &h, &buf)) return false;
    if (w <= 0 || h <= 0) { kfree(buf); return false; }
    out.width = w; out.height = h; out.pitch = w * 4;
    out.addr = buf;
    return true;
}

// Shared decode chain (host: GDI+ ; bare: stb -> PPM -> BMP -> JPEG).
// Used by both the Image Viewer and the Browser so web images render
// on bare metal too.
bool decode_image_any(const uint8_t* data, uint32_t size, Surface& out) {
    if (platform_decode_image(data, size, out)) return true;
    if (stbi_decode(data, size, out)) return true;
    if (ppm_decode(data, size, out)) return true;
    if (bmp_decode(data, size, out)) return true;
    if (jpeg_decode(data, size, out)) return true;
    return false;
}

static void image_load_current(ImageViewState* st) {
    if (st->src.addr) { kfree(st->src.addr); st->src.addr = 0; }
    st->src_ok = false;
    if (st->index < 0 || st->index >= st->files.size()) return;
    FSNode* f = st->files[st->index];
    if (!f || f->is_dir || f->size == 0) return;
    decode_image_any(f->data, f->size, st->src);
    st->src_ok = st->src.addr != 0;
    st->dirty = true;
}

static void image_scan_dir(ImageViewState* st) {
    st->files.clear();
    FSNode* d = g_vfs->resolve(PIC_DIR);
    if (!d || !d->is_dir) return;
    for (int i = 0; i < d->children.size(); i++) {
        FSNode* c = d->children[i];
        if (c->is_dir) continue;
        const char* n = c->name.c_str();
        int len = c->name.len();
        if (is_image_ext(n, len) || is_image_ext3(n, len) || strcmp(n + len - 4, ".img") == 0)
            st->files.push(c);
    }
    if (st->files.empty()) return;
    st->index = 0;
}

static void vfs_put_ppm(const char* path, int w, int h, uint32_t (*f)(int x, int y, int w, int h)) {
    char head[64];
    int hn = ksprintf(head, sizeof(head), "P6\n%d %d\n255\n", w, h);
    uint32_t sz = (uint32_t)hn + (uint32_t)w * (uint32_t)h * 3;
    uint8_t* buf = (uint8_t*)kalloc(sz);
    if (!buf) return;
    memcpy(buf, head, (size_t)hn);
    uint8_t* p = buf + hn;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = f(x, y, w, h);
            *p++ = (uint8_t)((c >> 16) & 0xFF);
            *p++ = (uint8_t)((c >> 8) & 0xFF);
            *p++ = (uint8_t)(c & 0xFF);
        }
    }
    FSNode* fnode = g_vfs->resolve(path);
    if (!fnode) {
        g_vfs->mkdir(PIC_DIR);
        fnode = g_vfs->create_file(path);
    }
    if (fnode) g_vfs->write_file(fnode, buf, sz);
    kfree(buf);
}

static uint32_t gen_gradient(int x, int y, int w, int h) {
    int r = 60 + x * 160 / w;
    int g = 90 + y * 130 / h;
    int b = 200 - (x + y) * 80 / (w + h);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t gen_stars(int x, int y, int w, int h) {
    (void)w; (void)h;
    uint32_t hsh = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
    hsh ^= hsh >> 13;
    if ((hsh & 0xFFFF) < 900) {
        int b = 180 + (int)(hsh & 0x3F);
        return ((uint32_t)b << 16) | ((uint32_t)b << 8) | (uint32_t)b;
    }
    return 0x0010182E;
}
static uint32_t gen_waves(int x, int y, int w, int h) {
    (void)w; (void)h;
    int s = (int)((y + 20 * (x % 3 == 0)) / 4);
    int v = 60 + 40 * s;
    int phase = (x / 8 + y / 3) & 3;
    if (phase == 0) v += 40;
    int b = (int)((v + 120) % 256);
    return ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)b;
}
static uint32_t gen_checker(int x, int y, int w, int h) {
    (void)w; (void)h;
    int cell = 40;
    int cx = x / cell, cy = y / cell;
    if ((cx + cy) & 1) return 0x00E8E4DC;
    return 0x004A5468;
}
static uint32_t gen_rings(int x, int y, int w, int h) {
    int dx = x - w / 2, dy = y - h / 2;
    int r = gfx::sqrti(dx * dx + dy * dy);
    int ring = r / 18;
    if (ring & 1) return 0x002F6FB6;
    return 0x00F4F3EE;
}
static uint32_t gen_hills(int x, int y, int w, int h) {
    int sky = 130 - y * 90 / h;
    uint32_t c = ((uint32_t)(80 + sky) << 16) | ((uint32_t)(150 + sky) << 8) | 220u;
    int d1 = (x - w / 3); if (d1 < 0) d1 = -d1;
    int d2 = (x - w * 2 / 3); if (d2 < 0) d2 = -d2;
    int y1 = h - 40 - d1 * 2 / 3;
    int y2 = h - 30 - d2 * 2 / 5;
    if (y >= y1) c = 0x003A5F43;
    if (y >= y2) c = 0x002E4C36;
    return c;
}

static void ensure_gallery() {
    FSNode* d = g_vfs->resolve(PIC_DIR);
    bool has = false;
    if (d && d->is_dir) {
        for (int i = 0; i < d->children.size(); i++) {
            if (!d->children[i]->is_dir) { has = true; break; }
        }
    }
    if (has) return;
    g_vfs->mkdir(PIC_DIR);
    auto put = [](const char* name, int w, int h, uint32_t (*f)(int, int, int, int)) {
        char path[96];
        ksprintf(path, sizeof(path), "%s/%s", PIC_DIR, name);
        vfs_put_ppm(path, w, h, f);
    };
    put("gradient.ppm", 400, 300, gen_gradient);
    put("stars.ppm", 400, 300, gen_stars);
    put("waves.ppm", 400, 300, gen_waves);
    put("checker.ppm", 400, 300, gen_checker);
    put("rings.ppm", 400, 300, gen_rings);
    put("hills.ppm", 400, 300, gen_hills);
}

static void img_wm_paint(Window* w) {
    ImageViewState* st = (ImageViewState*)w->userdata;
    int W = st->w, H = st->h;
    Surface& s = w->back;
    s.fill(color::CREAM);
    gfx::fillrect(s, 0, 0, W, 30, 0x00E7E6E1);
    gfx::hline(s, 0, W - 1, 30, color::BORDER);
    // toolbar buttons < > - 100% + Fit
    const char* labels[6] = { "<", ">", "-", "100%", "+", "Fit" };
    for (int i = 0; i < 6; i++) {
        int bx = 8 + i * 48;
        gfx::fillrect(s, bx, 6, 40, 22, 0x009AA5B1);
        gfx::rect(s, bx, 6, 40, 22, 0x007A8591);
        gfx::text(s, bx + (i == 3 ? 6 : 14), 11, labels[i], color::WHITE, 0x009AA5B1);
    }
    gfx::fillrect(s, 0, 0, W, 30, 0x00E7E6E1);
    gfx::hline(s, 0, W - 1, 30, color::BORDER);
    const char* name = "";
    if (st->index >= 0 && st->index < st->files.size()) name = st->files[st->index]->name.c_str();
    char info[96];
    ksprintf(info, sizeof(info), "%d/%d  %s", st->files.size() > 0 ? st->index + 1 : 0,
             st->files.size(), name);
    gfx::text(s, W - gfx::text_width(info) - 10, 7, info, color::TEXT2, 0x00E7E6E1);
    int avail_w = W - 16, avail_h = H - 30 - 24;
    if (st->dirty) image_recalc_scale(st, avail_w, avail_h);
    if (st->src_ok && st->scaled.addr) {
        int dx = 8 + (avail_w - st->scaled.width) / 2;
        int dy = 38 + (avail_h - st->scaled.height) / 2;
        if (dx < 8) dx = 8;
        if (dy < 38) dy = 38;
        gfx::blit_clip(s, st->scaled, dx, dy, 0, 0, avail_w, avail_h);
        gfx::rect(s, dx - 1, 37, avail_w + 2, avail_h + 2, color::BORDER);
    } else {
        gfx::text(s, W / 2 - 60, H / 2 - 8, "No image to display", color::TEXT2, color::CREAM);
    }
    gfx::fillrect(s, 0, H - 24, W, 24, 0x00E7E6E1);
    gfx::hline(s, 0, W - 1, H - 24, color::BORDER);
    if (st->src_ok) {
        ksprintf(info, sizeof(info), "%dx%d   zoom %s", st->src.width, st->src.height,
                 st->zoom == 0 ? "fit" : "100%+");
        gfx::text(s, 8, H - 18, info, color::TEXT2, 0x00E7E6E1);
    }
    gfx::text(s, W / 2, H - 18, "Open from File Manager (double-click an image)", color::TEXT2, 0x00E7E6E1);
}

static void img_wm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    ImageViewState* st = (ImageViewState*)w->userdata;
    if (!st || !buttons) return;
    // toolbar buttons < > - 100% + Fit at y 6..28, x 8+i*48
    if (my >= 6 && my < 28) {
        for (int i = 0; i < 6; i++) {
            int bx = 8 + i * 48;
            if (mx >= bx && mx < bx + 40) {
                if (i == 0 && st->files.size() > 0) {
                    st->index = (st->index - 1 + st->files.size()) % st->files.size();
                    image_load_current(st);
                } else if (i == 1 && st->files.size() > 0) {
                    st->index = (st->index + 1) % st->files.size();
                    image_load_current(st);
                } else if (i == 2) {
                    if (st->zoom == 0) st->zoom = 100; else st->zoom -= 25;
                    if (st->zoom < 25) st->zoom = 25;
                    st->dirty = true;
                } else if (i == 3) {
                    st->zoom = 100;
                    st->dirty = true;
                } else if (i == 4) {
                    if (st->zoom == 0) st->zoom = 100; else st->zoom += 25;
                    if (st->zoom > 400) st->zoom = 400;
                    st->dirty = true;
                } else if (i == 5) {
                    st->zoom = 0;
                    st->dirty = true;
                }
                return;
            }
        }
    }
}

static void imageviewer_open(FSNode* target) {
    ensure_gallery();
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Image Viewer", x, y, 620, 460);
    if (!w) return;
    ImageViewState* st = new ImageViewState();
    st->index = 0;
    st->zoom = 0;
    st->dirty = true;
    st->src_ok = false;
    st->src.addr = 0;
    st->scaled.addr = 0;
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = img_wm_paint;
    w->on_mouse = img_wm_mouse;

    image_scan_dir(st);
    if (target) {
        for (int i = 0; i < st->files.size(); i++) {
            if (st->files[i] == target) { st->index = i; break; }
        }
    }
    image_load_current(st);
    g_wm->raise(w);
}

void imageviewer_launch() { imageviewer_open(0); }
void app_show_image(FSNode* f) { imageviewer_open(f); }

} // namespace nefu