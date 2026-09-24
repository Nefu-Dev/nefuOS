// nefuOS graphics laboratory — 8 live demos for the gfxlib library
// Controls:
//   1..8  : pick a demo      Space : run / pause animation
//   R     : re-seed          ESC   : close
// Demos: 1 raster gallery, 2 polygon fill + clip, 3 2D transforms (Q16.16),
//        4 3D wireframe cube, 5 FBM noise terrain, 6 Mandelbrot zoom,
//        7 color palettes, 8 L-system tree + IFS + convex hull.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../gfxlib/gfxlib_all.h"

namespace nefu {

namespace {

const int LAB_W = 560, LAB_H = 420;

const char* LAB_NAMES[8] = {
    "Raster Gallery", "Polygon Fill/Clip", "2D Transform", "3D Wireframe",
    "Noise Terrain",  "Mandelbrot",        "Color Palettes", "Fractal Tree"
};

struct GfxLab {
    uint32_t* buf;                 // offscreen 0xAARRGGBB target
    int demo;                      // 0..7
    uint32_t tick;
    bool running;
    uint32_t seed;

    void reset(int d);
    void render();                 // draw the current demo into buf
    void paint(Surface& s);        // blit buf -> window surface
};

// ---- demo renderers (all write into the gfxlib Buffer) ----
static void render_raster(gfxlib::Buffer b, uint32_t t) {
    (void)t;
    gfxlib::Pixel line = 0xFF3498DB, aa = 0xFFE74C3C, c2 = 0xFF2ECC71, c3 = 0xFFF1C40F;
    // radial lines
    int cx = b.w / 2, cy = b.h / 2;
    for (int i = 0; i < 12; i++) {
        nefu::fx::fix a = nefu::fx::fx_deg2rad(nefu::fx::itofix(i * 30));
        int x = cx + nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(b.w / 2 - 20), nefu::fx::fx_cos(a)));
        int y = cy - nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(b.h / 2 - 20), nefu::fx::fx_sin(a)));
        gfxlib::draw_line(b, cx, cy, x, y, line);
    }
    // circles / ellipse
    gfxlib::draw_circle(b, 70, 70, 40, c2);
    gfxlib::draw_circle_fill(b, b.w - 70, 70, 30, 0x55FFFFFF);
    gfxlib::draw_ellipse(b, 70, b.h - 70, 50, 26, c2);
    // bezier + AA line
    gfxlib::draw_bezier(b, 90, b.h - 110, 160, 60, 260, 60, 330, b.h - 110, c3);
    gfxlib::draw_line_aa(b, 40, b.h - 40, b.w - 40, 40, aa);
    gfxlib::draw_rect(b, 12, 12, b.w - 24, b.h - 24, 0x55888888);
}

static void render_polyfill(gfxlib::Buffer b, uint32_t t) {
    (void)t;
    // star polygon filled + outlined
    {
        int n = 10;
        int xs[10], ys[10];
        int cx = 170, cy = 150;
        for (int i = 0; i < n; i++) {
            nefu::fx::fix a = nefu::fx::fx_deg2rad(nefu::fx::itofix(i * 36));
            int r = (i % 2) ? 100 : 40;
            xs[i] = cx + nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(r), nefu::fx::fx_cos(a)));
            ys[i] = cy - nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(r), nefu::fx::fx_sin(a)));
        }
        gfxlib::fill_polygon(b, xs, ys, n, 0xFFE67E22);
        gfxlib::draw_polygon(b, xs, ys, n, 0xFF7F3D00);
    }
    // clipped pentagon (window region)
    {
        int n = 5;
        int xs[5], ys[5];
        for (int i = 0; i < n; i++) {
            nefu::fx::fix a = nefu::fx::fx_deg2rad(nefu::fx::itofix(90 + i * 72));
            xs[i] = 380 + nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(130), nefu::fx::fx_cos(a)));
            ys[i] = 120 + nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(90), nefu::fx::fx_sin(a)));
        }
        gfxlib::fill_polygon(b, xs, ys, n, 0xFF3498DB);
        int ox[64], oy[64];
        int cn = gfxlib::clip_polygon_sutherland(xs, ys, n, 60, 40, 300, 250, ox, oy, 64);
        if (cn >= 3) {
            gfxlib::fill_polygon(b, ox, oy, cn, 0xFF2ECC71);
            gfxlib::draw_polygon(b, ox, oy, cn, 0xFF1B7A42);
        }
        gfxlib::draw_rect(b, 60, 40, 241, 211, 0xFFFFFFFF);
    }
    // a couple of clipped lines
    int x0 = 20, y0 = 20, x1 = 500, y1 = 380;
    if (gfxlib::clip_line_cohen(x0, y0, x1, y1, 60, 40, 300, 250))
        gfxlib::draw_line(b, x0, y0, x1, y1, 0xFFFF00FF);
}

static void render_2d(gfxlib::Buffer b, uint32_t t) {
    // rotating squares around the center (Q16.16 affine transforms)
    int cx = b.w / 2, cy = b.h / 2;
    for (int ring = 0; ring < 6; ring++) {
        nefu::fx::fix ang = nefu::fx::fx_deg2rad(
            nefu::fx::itofix((int)((t * (ring + 1) + ring * 40) % 360)));
        gfxlib::Mat2x3 rot = gfxlib::mat_mul(
            gfxlib::mat_translate(nefu::fx::itofix(cx), nefu::fx::itofix(cy)),
            gfxlib::mat_rotate(ang));
        int sq = 10 + ring * 8;
        int xs[4], ys[4];
        nefu::fx::fix px, py;
        gfxlib::mat_transform(rot, nefu::fx::itofix(-sq), nefu::fx::itofix(-sq), &px, &py);
        xs[0] = nefu::fx::fixtoi(px); ys[0] = nefu::fx::fixtoi(py);
        gfxlib::mat_transform(rot, nefu::fx::itofix(sq), nefu::fx::itofix(-sq), &px, &py);
        xs[1] = nefu::fx::fixtoi(px); ys[1] = nefu::fx::fixtoi(py);
        gfxlib::mat_transform(rot, nefu::fx::itofix(sq), nefu::fx::itofix(sq), &px, &py);
        xs[2] = nefu::fx::fixtoi(px); ys[2] = nefu::fx::fixtoi(py);
        gfxlib::mat_transform(rot, nefu::fx::itofix(-sq), nefu::fx::itofix(sq), &px, &py);
        xs[3] = nefu::fx::fixtoi(px); ys[3] = nefu::fx::fixtoi(py);
        gfxlib::draw_polygon(b, xs, ys, 4, 0xFF3498DBu + (uint32_t)(ring << 8));
    }
}

static void render_3d(gfxlib::Buffer b, uint32_t t) {
    // wireframe cube: rotate, project, draw 12 edges
    gfxlib::Mat3 m = gfxlib::mat3_mul(
        gfxlib::mat3_rotate_x(nefu::fx::fx_deg2rad(nefu::fx::itofix((int)(t % 360)))),
        gfxlib::mat3_mul(
            gfxlib::mat3_rotate_y(nefu::fx::fx_deg2rad(nefu::fx::itofix((int)((t * 2) % 360)))),
            gfxlib::mat3_rotate_z(nefu::fx::fx_deg2rad(nefu::fx::itofix((int)((t * 3) % 360))))));
    nefu::fx::fix half = nefu::fx::itofix(1);
    gfxlib::Vec3 v[8];
    int k = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int q = 0; q < 2; q++) {
                gfxlib::Vec3 p = { i ? half : -half, j ? half : -half, q ? half : -half };
                v[k++] = gfxlib::mat3_apply(m, p);
            }
    // cube edges
    static const int edges[12][2] = {
        {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}
    };
    nefu::fx::fix fov = nefu::fx::itofix(3);
    for (int e = 0; e < 12; e++) {
        int x0, y0, x1, y1;
        if (gfxlib::project(v[edges[e][0]], fov, b.w, b.h, &x0, &y0) &&
            gfxlib::project(v[edges[e][1]], fov, b.w, b.h, &x1, &y1))
            gfxlib::draw_line(b, x0, y0, x1, y1, 0xFF9B59B6);
    }
}

static void render_noise(gfxlib::Buffer b, uint32_t t) {
    // FBM heightfield coloured with the heatmap palette
    for (int y = 0; y < b.h; y += 2)
        for (int x = 0; x < b.w; x += 2) {
            nefu::fx::fix nx = nefu::fx::fxf(x, 90) + nefu::fx::fxf((int)(t % 4000), 64);
            nefu::fx::fix ny = nefu::fx::fxf(y, 90);
            nefu::fx::fix f = gfxlib::fbm2d(nx, ny, 4, nefu::fx::itofix(2),
                                              nefu::fx::fxf(5, 10), 0x1234ABCDu);
            f = f + nefu::fx::itofix(1);                       // shift to [0,2]
            if (f < 0) f = 0;
            if (f > nefu::fx::itofix(2)) f = nefu::fx::itofix(2);
            int tt = (int)(nefu::fx::fixtoi(f) * 128);         // [0,256]
            if (tt > 255) tt = 255;
            gfxlib::Pixel c = gfxlib::rgb_pack(gfxlib::palette_heatmap(tt));
            for (int dy = 0; dy < 2 && y + dy < b.h; dy++)
                for (int dx = 0; dx < 2 && x + dx < b.w; dx++)
                    gfxlib::draw_pixel(b, x + dx, y + dy, c);
        }
}

static void render_mandel(gfxlib::Buffer b, uint32_t t) {
    (void)t;
    // Mandelbrot: real in [-2.0, 0.7], imaginary in [-1.1, 1.1]
    int steps = 48;
    for (int y = 0; y < b.h; y += 2)
        for (int x = 0; x < b.w; x += 2) {
            nefu::fx::fix cx = nefu::fx::fxf(-2000 + x * 2700 / b.w, 1000);
            nefu::fx::fix cy = nefu::fx::fxf(-1100 + y * 2200 / b.h, 1000);
            int it = gfxlib::mandelbrot_escape(cx, cy, steps);
            gfxlib::Pixel c;
            if (it < 0) c = 0xFF000000;
            else c = gfxlib::rgb_pack(gfxlib::palette_rainbow((it * 48) & 255));
            for (int dy = 0; dy < 2 && y + dy < b.h; dy++)
                for (int dx = 0; dx < 2 && x + dx < b.w; dx++)
                    gfxlib::draw_pixel(b, x + dx, y + dy, c);
        }
}

static void render_color(gfxlib::Buffer b, uint32_t t) {
    (void)t;
    // three gradient bars + a hue wheel
    int y0 = 60;
    for (int x = 0; x < b.w; x++) {
        int tt = x * 255 / b.w;
        gfxlib::Pixel rc = gfxlib::rgb_pack(gfxlib::palette_rainbow(tt));
        gfxlib::Pixel hc = gfxlib::rgb_pack(gfxlib::palette_heatmap(tt));
        gfxlib::Pixel pc = gfxlib::rgb_pack(gfxlib::palette_plasma(tt));
        gfxlib::draw_pixel(b, x, y0, rc);
        gfxlib::draw_pixel(b, x, y0 + 16, hc);
        gfxlib::draw_pixel(b, x, y0 + 32, pc);
    }
    // hue wheel
    int cx = b.w / 2, cy = 300, r = 70;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            int deg = nefu::fx::fixtoi(nefu::fx::fx_rad2deg(
                nefu::fx::fx_atan2(nefu::fx::itofix(dy), nefu::fx::itofix(dx))));
            int hue = (deg + 360) % 360;
            gfxlib::HSL hsl;
            hsl.h = hue; hsl.s = 100; hsl.l = 50;
            gfxlib::draw_pixel(b, x, y, gfxlib::rgb_pack(gfxlib::hsl_to_rgb(hsl)));
        }
}

static void render_tree(gfxlib::Buffer b, uint32_t t) {
    (void)t;
    // L-system tree (turtle)
    gfxlib::LSystem ls;
    ls.axiom[0] = 'A'; ls.axiom[1] = 0;
    ls.ruleA[0] = 'B'; ls.ruleA[1] = 0;                       // A -> B
    ls.ruleB[0] = 'B'; ls.ruleB[1] = '['; ls.ruleB[2] = '+';
    ls.ruleB[3] = 'A'; ls.ruleB[4] = ']'; ls.ruleB[5] = '[';
    ls.ruleB[6] = '-'; ls.ruleB[7] = 'A'; ls.ruleB[8] = ']';
    ls.ruleB[9] = 0;                                          // B -> B[+A][-A]
    ls.angle = nefu::fx::fx_deg2rad(nefu::fx::itofix(25));
    ls.step = nefu::fx::itofix(2);
    ls.expand(4);
    // turtle: 'A','B' draw forward, '+'/'-' turn, '[' push, ']' pop
    struct Turtle { int x, y; nefu::fx::fix ang; };
    Turtle stack[64];
    int sp = 0;
    Turtle cur;
    cur.x = b.w / 2; cur.y = b.h - 30;
    cur.ang = -nefu::fx::FX_PI_2;                              // pointing up
    for (int i = 0; i < ls.out_len; i++) {
        char ch = ls.output[i];
        if (ch == 'A' || ch == 'B') {
            int nx = cur.x + nefu::fx::fixtoi(nefu::fx::fx_mul(ls.step, nefu::fx::fx_cos(cur.ang)));
            int ny = cur.y + nefu::fx::fixtoi(nefu::fx::fx_mul(ls.step, nefu::fx::fx_sin(cur.ang)));
            gfxlib::draw_line(b, cur.x, cur.y, nx, ny, 0xFF27AE60);
            cur.x = nx; cur.y = ny;
        } else if (ch == '+') cur.ang += ls.angle;
        else if (ch == '-') cur.ang -= ls.angle;
        else if (ch == '[') { if (sp < 64) stack[sp++] = cur; }
        else if (ch == ']') { if (sp > 0) cur = stack[--sp]; }
    }
    // IFS fern
    {
        nefu::fx::fix fx_ = 0, fy_ = 0;
        for (int i = 0; i < 3000; i++) {
            nefu::fx::fix r = (nefu::fx::fix)(gfxlib::hash_u32((uint32_t)i * 2654435761u + 7u) & 0xFFFFu);
            gfxlib::ifs_barnsley(r, &fx_, &fy_);
            int x = 340 + nefu::fx::fixtoi(nefu::fx::fx_mul(fx_, nefu::fx::itofix(80)));
            int y = 60 + nefu::fx::fixtoi(nefu::fx::fx_mul(fy_, nefu::fx::itofix(-90)));
            if (x >= 0 && x < b.w && y >= 0 && y < b.h)
                gfxlib::draw_pixel(b, x, y, 0xFF2ECC71);
        }
    }
    // convex hull of random points
    {
        gfxlib::Point pts[24];
        uint32_t sd = 0x5EED1234;
        for (int i = 0; i < 24; i++) {
            sd = sd * 1664525u + 1013904223u;
            pts[i].x = 40 + (int)((sd >> 8) % 140);
            sd = sd * 1664525u + 1013904223u;
            pts[i].y = 60 + (int)((sd >> 8) % 120);
        }
        gfxlib::Point hull[32];
        int hn = gfxlib::convex_hull(pts, 24, hull);
        for (int i = 0; i < 24; i++)
            gfxlib::draw_pixel(b, pts[i].x, pts[i].y, 0xFFFF0000);
        if (hn >= 3) {
            int xs[32], ys[32];
            for (int i = 0; i < hn; i++) { xs[i] = hull[i].x; ys[i] = hull[i].y; }
            gfxlib::draw_polygon(b, xs, ys, hn, 0xFF00BFFF);
        }
    }
}

void GfxLab::render() {
    gfxlib::Buffer b = { buf, LAB_W, LAB_H };
    gfxlib::draw_rect_fill(b, 0, 0, LAB_W, LAB_H, 0xFF101820);
    switch (demo) {
        case 0: render_raster(b, tick); break;
        case 1: render_polyfill(b, tick); break;
        case 2: render_2d(b, tick); break;
        case 3: render_3d(b, tick); break;
        case 4: render_noise(b, tick); break;
        case 5: render_mandel(b, tick); break;
        case 6: render_color(b, tick); break;
        default: render_tree(b, tick); break;
    }
}

void GfxLab::reset(int d) {
    demo = d;
    running = true;
    seed = (uint32_t)(platform_tick_ms() * 97) ^ 0xA5A5A5A5u;
    tick = 0;
}

void GfxLab::paint(Surface& s) {
    // blit the offscreen buffer into the window surface
    for (int y = 0; y < LAB_H && y < s.height; y++) {
        const uint32_t* row = buf + (size_t)y * LAB_W;
        for (int x = 0; x < LAB_W && x < s.width; x++)
            s.setpx(x, y, row[x]);
    }
    // caption
    char cap[128];
    ksprintf(cap, sizeof(cap), "Graphics Lab: %s   [1-8] demo   [Space] %s   [R] re-seed",
             LAB_NAMES[demo], running ? "pause" : "run");
    gfx::text(s, 8, 4, cap, 0x00DDDDDD, 0x00101820);
}

} // namespace

// ---- window glue ----
static GfxLab* lab_of(Window* w) { return (GfxLab*)w->userdata; }

static void lab_paint(Window* w) { lab_of(w)->paint(w->back); }

static void lab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    GfxLab* l = lab_of(w);
    if (e->ascii == 'r' || e->ascii == 'R') { l->reset(l->demo); return; }
    if (e->keycode == KEY_SPACE) { l->running = !l->running; return; }
    if (e->ascii >= '1' && e->ascii <= '8') { l->reset(e->ascii - '1'); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void lab_tick(Window* w) {
    GfxLab* l = lab_of(w);
    if (l && l->running) l->tick++;
}

static void lab_close(Window* w) {
    GfxLab* l = (GfxLab*)w->userdata;
    if (l) { delete[] l->buf; delete l; }
    w->userdata = 0;
}

void gfxlab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Graphics Lab", x, y, LAB_W, LAB_H);
    if (!w) return;
    GfxLab* l = new GfxLab();
    l->buf = new uint32_t[(size_t)LAB_W * LAB_H];
    l->reset(0);
    w->userdata = l;
    w->on_paint = lab_paint;
    w->on_key = lab_key;
    w->on_tick = lab_tick;
    w->on_close = lab_close;
    g_wm->raise(w);
}

} // namespace nefu
