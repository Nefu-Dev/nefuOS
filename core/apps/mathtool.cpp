// nefuOS 数学工具箱 —— mathext 库的窗口演示应用
// 参考 algoviz.cpp 模式：单窗口 + on_paint/on_key/on_close 回调。
// 三页演示：
//   [1] 矩阵：行列式、逆、特征值（幂迭代）
//   [2] 统计：均值/方差/线性回归（最小二乘）
//   [3] 多项式：x^2-3x+2 求根
// 按键 1/2/3 切页，ESC 关闭。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../mathext/mathext_all.h"

namespace nefu {

namespace {

using namespace mathext;
const int MT_W = 640, MT_H = 440;

// ksprintf 不支持 %f，这里手写一个 double -> "xx.xx" 的小格式化器。
void fmt_dbl(char* out, size_t outsz, double v) {
    // 处理符号与整数/小数部分（保留 2 位小数）
    char tmp[32];
    int pos = 0;
    bool neg = v < 0;
    double av = neg ? -v : v;
    long long int_part = (long long)av;
    double frac = av - (double)int_part;
    long long frac2 = (long long)(frac * 100.0 + 0.5);
    if (frac2 >= 100) { int_part++; frac2 -= 100; }
    // 拼整数部分（逆序）
    char buf[24];
    int bp = 0;
    if (int_part == 0) buf[bp++] = '0';
    while (int_part > 0 && bp < 23) {
        buf[bp++] = (char)('0' + (int)(int_part % 10));
        int_part /= 10;
    }
    if (neg && pos == 0) {}
    int n = 0;
    if (neg) out[n++] = '-';
    for (int i = bp - 1; i >= 0 && n < (int)outsz - 1; i--) out[n++] = buf[i];
    out[n++] = '.';
    out[n++] = (char)('0' + (int)(frac2 / 10));
    out[n++] = (char)('0' + (int)(frac2 % 10));
    out[n] = 0;
    (void)tmp;
}

struct MathTool {
    int page;   // 0/1/2
    char lines[6][128];
    int  nlines;

    void rebuild();
    void paint(Surface& s);
};

void MathTool::rebuild() {
    nlines = 0;
    char* L = lines[nlines];
    if (page == 0) {
        // ---- 矩阵演示 ----
        double a[4] = {1, 2, 3, 4};
        Matrix A(2, 2, a);
        L = lines[nlines++];
        ksprintf(L, 128, "Matrix demo: A = [[1,2],[3,4]]");
        double d = mat_determinant(A);
        L = lines[nlines++];
        ksprintf(L, 128, "det(A) = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), d);
        Matrix Ai = mat_inverse(A);
        L = lines[nlines++];
        ksprintf(L, 128, "inv(A) = [[");
        char b[24]; fmt_dbl(b, 24, Ai(0, 0));
        ksprintf(L, 128, "inv(A)[0][0]=");
        strcat(L, b);
        // 幂迭代特征值
        double dd[4] = {2, 0, 0, 1};
        Matrix D(2, 2, dd);
        double vec[2];
        double eig = eigen_power(D, vec);
        L = lines[nlines++];
        ksprintf(L, 128, "eig diag(2,1) = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), eig);
        L = lines[nlines++];
        ksprintf(L, 128, "Press 1/2/3 to switch page, Esc to close");
    } else if (page == 1) {
        // ---- 统计演示 ----
        double x[5] = {1, 2, 3, 4, 5};
        double y[5] = {2, 4, 5, 4, 5};
        double m = stat_mean(x, 5);
        double sd = stat_stddev(x, 5);
        double slope, ic;
        linreg(x, y, 5, slope, ic);
        L = lines[nlines++];
        ksprintf(L, 128, "Statistics demo: x=[1..5]");
        L = lines[nlines++];
        ksprintf(L, 128, "mean(x) = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), m);
        L = lines[nlines++];
        ksprintf(L, 128, "stddev(x) = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), sd);
        L = lines[nlines++];
        ksprintf(L, 128, "linreg slope = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), slope);
        L = lines[nlines++];
        ksprintf(L, 128, "linreg intercept = ");
        fmt_dbl(L + strlen(L), 128 - strlen(L), ic);
    } else {
        // ---- 多项式求根 ----
        double p[3] = {2, -3, 1};   // x^2 - 3x + 2
        double roots[8];
        int rc = poly_roots(p, 3, -10, 10, roots, 8);
        L = lines[nlines++];
        ksprintf(L, 128, "Polynomial: x^2 - 3x + 2");
        L = lines[nlines++];
        ksprintf(L, 128, "roots found: ");
        for (int i = 0; i < rc && i < 2; i++) {
            char b[24]; fmt_dbl(b, 24, roots[i]);
            strcat(L, " ");
            strcat(L, b);
        }
        L = lines[nlines++];
        ksprintf(L, 128, "expected roots: 1.00, 2.00");
        L = lines[nlines++];
        ksprintf(L, 128, "gcd(48,18)=6  prime(997)=yes  prime(561)=no");
    }
}

void MathTool::paint(Surface& s) {
    s.fill(0x00FAF8EF);
    gfx::text_scale(s, 10, 8, "Math Toolbox (mathext)", 0x00776756, 0x00FAF8EF, 2);
    char buf[96];
    ksprintf(buf, sizeof(buf), "Page %d/3  [1]Matrix [2]Stats [3]Poly", page + 1);
    gfx::text(s, 10, 42, buf, 0x00505050, 0x00FAF8EF);
    for (int i = 0; i < nlines; i++) {
        gfx::text(s, 10, 80 + i * 22, lines[i], 0x00202020, 0x00FAF8EF);
    }
    gfx::text(s, 10, MT_H - 18, "1/2/3: switch   Esc: close", 0x00909090, 0x00FAF8EF);
}

} // namespace

// ---- 窗口胶水 ----
static MathTool* mt_of(Window* w) { return (MathTool*)w->userdata; }

static void mt_paint(Window* w) { mt_of(w)->paint(w->back); }

static void mt_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    MathTool* t = mt_of(w);
    if (e->ascii == '1' || e->ascii == '2' || e->ascii == '3') {
        t->page = e->ascii - '1';
        t->rebuild();
        return;
    }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}

static void mt_close(Window* w) {
    if (w->userdata) delete (MathTool*)w->userdata;
    w->userdata = 0;
}

void mathtool_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Math Toolbox", x, y, MT_W, MT_H);
    if (!w) return;
    MathTool* t = new MathTool();
    t->page = 0;
    t->rebuild();
    w->userdata = t;
    w->on_paint = mt_paint;
    w->on_key = mt_key;
    w->on_close = mt_close;
    g_wm->raise(w);
}

} // namespace nefu
