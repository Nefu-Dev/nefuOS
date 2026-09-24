// nefuOS 机器学习实验室 —— 窗口应用
// 参考 algoviz.cpp 模式：create_window + on_paint/on_key/on_close。
// 展示内容：合成二维数据集上的 K-Means 聚类可视化（散点按簇着色，质心画十字）。
//   R : 用新种子重新生成数据并聚类
//   K : 切换 K 值 (2..4)
//   空格 : 重新聚类
//   ESC : 关闭
// 所有计算走 nefu::ml 的 Q16.16 定点算法，无 FPU 依赖。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../ml/ml_all.h"

namespace nefu {
namespace mlapp {
using namespace ml;
using fx::fix;

const int W = 520, H = 440;
const int MAXPTS = 120;

struct MlLab {
    int   n;                 // 样本数
    fix   X[MAXPTS][2];      // 原始数据（fx）
    int   assign[MAXPTS];     // KMeans 簇分配
    int   k;
    KMeans km;
    uint32_t seed;

    void gen(uint32_t s) {
        seed = s;
        Rng rng(s);
        n = MAXPTS;
        // 三个高斯团：中心分别在 (80,80),(400,100),(200,320) 的像素坐标域
        fix cx[3] = {fx::itofix(120), fx::itofix(400), fx::itofix(240)};
        fix cy[3] = {fx::itofix(100), fx::itofix(120), fx::itofix(340)};
        for (int i = 0; i < n; i++) {
            int c = i % 3;
            // 中心 + 小扰动（±30px）
            fix dx = rng.range_fx(-fx::itofix(35), fx::itofix(35));
            fix dy = rng.range_fx(-fx::itofix(35), fx::itofix(35));
            X[i][0] = cx[c] + dx;
            X[i][1] = cy[c] + dy;
        }
        run();
    }
    void run() {
        km.free_centers();
        fix flat[MAXPTS * 2];
        for (int i = 0; i < n; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }
        km.fit(flat, n, 2, k, seed ^ 0x51);
        for (int i = 0; i < n; i++) assign[i] = km.labels[i];
    }
    void paint(Surface& s) {
        // 背景
        gfx::fillrect(s, 0, 0, W, H, color::CREAM);
        // 标题栏提示
        gfx::text(s, 8, 6, "ML Lab: K-Means clustering", color::TEXT, color::CREAM);
        // 把 fx 坐标画到屏幕。fx 值域约 [0,520]x[0,440]
        uint32_t pal[4] = {color::RED, color::BLUE, color::GREEN, color::ORANGE};
        for (int i = 0; i < n; i++) {
            int px = fx::fixtoi(X[i][0]);
            int py = fx::fixtoi(X[i][1]);
            int c = assign[i] % 4;
            gfx::fillcircle(s, px, py, 3, pal[c]);
        }
        // 质心画十字
        for (int c = 0; c < k; c++) {
            fix mx = km.centers[c*2];
            fix my = km.centers[c*2+1];
            int cx = fx::fixtoi(mx);
            int cy = fx::fixtoi(my);
            uint32_t col = pal[c % 4];
            gfx::line(s, cx - 8, cy, cx + 8, cy, col);
            gfx::line(s, cx, cy - 8, cx, cy + 8, col);
        }
        // 底部帮助
        char buf[48];
        ksprintf(buf, sizeof(buf), "k=%d   R:regen  K:k++  Space:recluster  Esc:close", k);
        gfx::text(s, 8, H - 16, buf, color::TEXT2, color::CREAM);
    }
};

} // namespace mlapp

static mlapp::MlLab* lab_of(Window* w) { return (mlapp::MlLab*)w->userdata; }

static void mllab_paint(Window* w) { lab_of(w)->paint(w->back); }

static void mllab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    mlapp::MlLab* m = lab_of(w);
    if (e->ascii == 'r' || e->ascii == 'R') { m->gen(platform_tick_ms()); return; }
    if (e->ascii == 'k' || e->ascii == 'K') { m->k = m->k >= 4 ? 2 : m->k + 1; m->run(); return; }
    if (e->keycode == KEY_SPACE) { m->run(); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void mllab_close(Window* w) {
    if (w->userdata) delete (mlapp::MlLab*)w->userdata;
    w->userdata = 0;
}

void mllab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("ML Lab", x, y, mlapp::W, mlapp::H);
    if (!w) return;
    mlapp::MlLab* m = new mlapp::MlLab();
    m->k = 3;
    m->gen(0xC0FFEE);
    w->userdata = m;
    w->on_paint = mllab_paint;
    w->on_key = mllab_key;
    w->on_close = mllab_close;
    g_wm->raise(w);
}

} // namespace nefu
