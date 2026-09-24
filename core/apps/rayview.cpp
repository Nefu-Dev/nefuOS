// ============================================================================
// nefuOS 光线追踪查看器 —— core/apps/rayview.cpp
// ----------------------------------------------------------------------------
// 在窗口里逐行渐进渲染预设场景（康奈尔盒/三球/玻璃球）。
//   1 / 2 / 3 : 切换预设场景
//   Esc       : 关闭窗口
// 渲染在 on_tick 里每次推进若干行，避免阻塞 UI；on_paint 把低分辨率
// 帧缓冲放大 blit 到窗口并显示进度。
// ============================================================================
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../raytrace/raytrace_all.h"

namespace nefu {
namespace raytrace {
namespace {

const int RV_W = 640, RV_H = 480;     // 窗口尺寸
const int RB_W = 160, RB_H = 120;     // 内部渲染分辨率（低分辨率提速）

struct RayView {
    Scene    scene;
    uint32_t fb[RB_W * RB_H];
    int      cur_row;      // 已渲染到哪一行
    int      which;        // 当前预设
    bool     dirty;

    RayView() : cur_row(0), which(0), dirty(true) {
        scene.load_preset(which);
    }

    void switch_scene(int w) {
        which = w;
        scene.load_preset(which);
        cur_row = 0;
        dirty = true;
    }

    // 推进若干行渲染
    void tick() {
        if (cur_row >= RB_H) return;
        // 每次推进 2 行，留出 UI 响应时间
        for (int step = 0; step < 2 && cur_row < RB_H; step++) {
            RTRng rng(0x9e3779b9u ^ (uint32_t)cur_row);
            for (int x = 0; x < RB_W; x++) {
                rtfx u = rt_div(rt_itofx(x) + rng.next_fx(), rt_itofx(RB_W));
                rtfx v = rt_div(rt_itofx(RB_H - 1 - cur_row) + rng.next_fx(), rt_itofx(RB_H));
                RTVec3 c = scene.shade_pixel(u, v, rng);
                // 简易 tonemap
                rtfx rr = rt_div(c.x, RT_ONE + c.x);
                rtfx gg = rt_div(c.y, RT_ONE + c.y);
                rtfx bb = rt_div(c.z, RT_ONE + c.z);
                rr = rt_sqrt(rr); gg = rt_sqrt(gg); bb = rt_sqrt(bb);
                int R = rt_fxtoi(rr * rt_itofx(255));
                int G = rt_fxtoi(gg * rt_itofx(255));
                int B = rt_fxtoi(bb * rt_itofx(255));
                if (R < 0) R = 0; if (R > 255) R = 255;
                if (G < 0) G = 0; if (G > 255) G = 255;
                if (B < 0) B = 0; if (B > 255) B = 255;
                fb[cur_row * RB_W + x] = ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
            }
            cur_row++;
        }
        dirty = true;
    }

    void paint(Surface& s) {
        // 背景
        gfx::fillrect(s, 0, 0, RV_W, RV_H, 0x00101418);
        // 把低分辨率 fb 放大 blit
        int sx = (RV_W - RB_W * 3) / 2;
        int sy = 40;
        for (int y = 0; y < RB_H; y++) {
            uint32_t row = 0; (void)row;
            for (int x = 0; x < RB_W; x++) {
                gfx::fillrect(s, sx + x * 3, sy + y * 3, 3, 3, fb[y * RB_W + x]);
            }
        }
        // 标题与进度
        const char* names[3] = { "Cornell Box", "Three Spheres", "Glass Sphere" };
        gfx::text_scale(s, 10, 8, "Ray Tracer", 0x00ECF0F1, 0x00101418, 2);
        gfx::text(s, 10, 28, names[which], 0x003498DB, 0x00101418);
        int pct = cur_row * 100 / RB_H;
        char buf[32];
        // ksprintf 不支持 %f，用 %d
        ksprintf(buf, sizeof(buf), "Rendering %d%%", pct);
        gfx::text(s, RV_W - 120, 8, buf, 0x002ECC71, 0x00101418);
        gfx::text(s, 10, RV_H - 16, "1:Cornell 2:Spheres 3:Glass  Esc:close",
                  0x00909090, 0x00101418);
    }
};

} // namespace

} // namespace raytrace
} // namespace nefu

// 注意：本文件在 nefu 命名空间外组织窗口胶水，避免与 raytrace 内部符号冲突
using namespace nefu;
using namespace nefu::raytrace;

static RayView* rv_of(Window* w) { return (RayView*)w->userdata; }

static void rv_paint(Window* w) { rv_of(w)->paint(w->back); }

static void rv_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    RayView* v = rv_of(w);
    if (e->ascii == '1') v->switch_scene(0);
    else if (e->ascii == '2') v->switch_scene(1);
    else if (e->ascii == '3') v->switch_scene(2);
    else if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void rv_tick(Window* w) { rv_of(w)->tick(); }

static void rv_close(Window* w) {
    if (w->userdata) delete (RayView*)w->userdata;
    w->userdata = 0;
}

void rayview_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Ray Trace Viewer", x, y, RV_W, RV_H);
    if (!w) return;
    RayView* v = new RayView();
    w->userdata = v;
    w->on_paint = rv_paint;
    w->on_key = rv_key;
    w->on_tick = rv_tick;
    w->on_close = rv_close;
    g_wm->raise(w);
}
