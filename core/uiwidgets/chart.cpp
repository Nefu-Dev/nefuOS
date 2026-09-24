// nefuOS UI 组件库 —— Chart 实现
#include "chart.h"
#include <string.h>

namespace nefu {
namespace ui {

// ---- 整数单位圆查表(30 度间隔,放大 1000 倍),避免 libm 依赖 ----
// 角度 0=+x 方向,逆时针增大(数学约定)
static const int UC_X[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
static const int UC_Y[12] = {   0, -500, -866, -1000, -866, -500,    0,  500,  866, 1000, 866, 500};
// 给定角度(度,0..359)与半径 r,输出屏幕坐标偏移(sy 已翻转:y 向上)
static void uc_point(int deg, int r, int& dx, int& dy) {
    int d = ((deg % 360) + 360) % 360;
    int idx = d / 30;
    int frac = d % 30;
    long long x = UC_X[idx] + (long long)(UC_X[(idx + 1) % 12] - UC_X[idx]) * frac / 30;
    long long y = UC_Y[idx] + (long long)(UC_Y[(idx + 1) % 12] - UC_Y[idx]) * frac / 30;
    dx = (int)(x * r / 1000);
    dy = (int)(-y * r / 1000);   // 屏幕 y 向下,翻转
}

Chart::Chart(Widget* parent) : Widget(parent),
    type_(ChartLine), ylo_(0), yhi_(100), xlo_(0), xhi_(1), realtime_max_(50) {
    pref_w_ = 280; pref_h_ = 200;
}

int Chart::add_series(const char* name, gfxlib::Pixel color) {
    names_.push(String(name));
    colors_.push(color);
    values_.push(List<double>());
    points_.push(List<ChartPoint>());
    return names_.size() - 1;
}
void Chart::clear_series(int s) {
    if (s >= 0 && s < values_.size()) { values_[s].clear(); points_[s].clear(); }
}
void Chart::add_value(int s, double v) {
    if (s >= 0 && s < values_.size()) values_[s].push(v);
    invalidate();
}
void Chart::add_point(int s, double x, double y) {
    if (s >= 0 && s < points_.size()) { ChartPoint p = { x, y }; points_[s].push(p); }
    invalidate();
}
int Chart::point_count(int s) const {
    if (s < 0 || s >= values_.size()) return 0;
    return values_[s].size();
}

int Chart::value_to_px(double v) const {
    double span = yhi_ - ylo_;
    if (span <= 0) span = 1.0;
    double t = (v - ylo_) / span;            // 0..1
    if (t < 0) t = 0; if (t > 1) t = 1;
    int bottom = plot_bottom(), top = plot_top();
    return bottom - (int)(t * (bottom - top));
}
int Chart::index_to_px(int i, int n) const {
    if (n <= 0) n = 1;
    int left = plot_left(), right = plot_right();
    return left + (i + 1) * (right - left) / (n + 1);
}

void Chart::push_realtime(double v) {
    if (values_.empty()) add_series("realtime", 0xFF3498DBu);
    values_[0].push(v);
    while (values_[0].size() > realtime_max_) values_[0].remove(0);
    invalidate();
}

// ---- 绘制 ----
void Chart::draw_axes(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    int l = ox + plot_left(), r = ox + plot_right();
    int tp = oy + plot_top(), bt = oy + plot_bottom();
    // 坐标轴
    gfxlib::draw_line(b, l, tp, l, bt, t.border);
    gfxlib::draw_line(b, l, bt, r, bt, t.border);
    // 水平网格(4 条)
    for (int i = 1; i < 4; i++) {
        int gy = tp + (bt - tp) * i / 4;
        gfxlib::draw_line(b, l, gy, r, gy, 0xFFE5E9ECu);
    }
}

void Chart::draw_line_chart(gfxlib::Buffer b, int ox, int oy) {
    draw_axes(b, ox, oy);
    for (int s = 0; s < values_.size(); s++) {
        int n = values_[s].size();
        if (n < 2) continue;
        gfxlib::Pixel c = colors_[s];
        int prev_x = 0, prev_y = 0;
        for (int i = 0; i < n; i++) {
            int px = ox + index_to_px(i, n);
            int py = oy + value_to_px(values_[s][i]);
            if (i > 0) gfxlib::draw_line(b, prev_x, prev_y, px, py, c);
            prev_x = px; prev_y = py;
        }
    }
}

void Chart::draw_area_chart(gfxlib::Buffer b, int ox, int oy) {
    draw_axes(b, ox, oy);
    for (int s = 0; s < values_.size(); s++) {
        int n = values_[s].size();
        if (n < 2) continue;
        gfxlib::Pixel c = colors_[s];
        int base = oy + plot_bottom();
        int prev_x = 0, prev_y = 0;
        for (int i = 0; i < n; i++) {
            int px = ox + index_to_px(i, n);
            int py = oy + value_to_px(values_[s][i]);
            if (i > 0) {
                // 梯形填充(用垂直条近似)
                gfxlib::draw_line(b, prev_x, prev_y, px, py, c);
                for (int yy = prev_y; yy < base; yy += 2)
                    gfxlib::draw_line(b, prev_x, yy, px, yy, c);
            }
            prev_x = px; prev_y = py;
        }
    }
}

void Chart::draw_bar_chart(gfxlib::Buffer b, int ox, int oy) {
    draw_axes(b, ox, oy);
    int series = values_.size();
    if (series <= 0) return;
    int n = values_[0].size();
    int slot = (plot_right() - plot_left()) / (n > 0 ? n : 1);
    int bw = slot / (series + 1);
    for (int i = 0; i < n; i++) {
        int bx = ox + plot_left() + i * slot + 2;
        for (int s = 0; s < series; s++) {
            if (i >= values_[s].size()) continue;
            int py = oy + value_to_px(values_[s][i]);
            int base = oy + plot_bottom();
            gfxlib::draw_rect_fill(b, bx + s * bw, py, bw - 1, base - py, colors_[s]);
        }
    }
}

void Chart::draw_pie_chart(gfxlib::Buffer b, int ox, int oy) {
    int s = 0;
    if (values_.empty() || values_[0].size() == 0) return;
    double total = 0;
    for (int i = 0; i < values_[0].size(); i++) total += values_[0][i];
    if (total <= 0) total = 1;
    int cx = ox + w / 2, cy = oy + (plot_top() + plot_bottom()) / 2;
    int r = (plot_bottom() - plot_top()) / 2 - 4;
    int acc = 0;
    gfxlib::Pixel palette[6] = {
        0xFF3498DBu, 0xFFE74C3Cu, 0xFF2ECC71u, 0xFFF39C12u, 0xFF9B59B6u, 0xFF1ABC9Cu
    };
    for (int i = 0; i < values_[0].size(); i++) {
        int sweep = (int)(values_[0][i] * 360.0 / total);
        // 用多边形近似扇区:中心点 + 圆弧上若干点
        int xs[128], ys[128];
        int nv = 0;
        xs[nv] = cx; ys[nv] = cy; nv++;
        int steps = sweep / 10; if (steps < 1) steps = 1;
        for (int a = 0; a <= sweep && nv < 126; a += 10) {
            int dx, dy;
            uc_point(acc + a, r, dx, dy);
            xs[nv] = cx + dx; ys[nv] = cy + dy; nv++;
        }
        // 闭合回圆心
        xs[nv] = cx; ys[nv] = cy; nv++;
        gfxlib::fill_polygon(b, xs, ys, nv, palette[i % 6]);
        acc += sweep;
    }
}

void Chart::draw_scatter(gfxlib::Buffer b, int ox, int oy) {
    draw_axes(b, ox, oy);
    for (int s = 0; s < points_.size(); s++) {
        for (int i = 0; i < points_[s].size(); i++) {
            int px = ox + (int)points_[s][i].x;
            int py = oy + (int)points_[s][i].y;
            gfxlib::draw_circle_fill(b, px, py, 2, colors_[s]);
        }
    }
}

void Chart::draw_radar(gfxlib::Buffer b, int ox, int oy) {
    int cx = ox + w / 2, cy = oy + h / 2;
    int r = (h - 20) / 2;
    // 五边形网格
    for (int ring = 1; ring <= 3; ring++) {
        int xs[5], ys[5];
        for (int i = 0; i < 5; i++) {
            int deg = i * 72 - 90;     // 顶点朝上
            int dx, dy;
            uc_point(deg, r * ring / 3, dx, dy);
            xs[i] = cx + dx; ys[i] = cy + dy;
        }
        gfxlib::draw_polygon(b, xs, ys, 5, 0xFFD0D7DEu);
    }
    // 数据多边形
    if (!values_.empty() && values_[0].size() >= 3) {
        int n = values_[0].size();
        int xs[8], ys[8];
        for (int i = 0; i < n && i < 8; i++) {
            int deg = i * 360 / n - 90;
            double v = values_[0][i] / (yhi_ > 0 ? yhi_ : 1.0);
            if (v > 1) v = 1;
            int dx, dy;
            uc_point(deg, (int)(r * v), dx, dy);
            xs[i] = cx + dx; ys[i] = cy + dy;
        }
        gfxlib::fill_polygon(b, xs, ys, n, 0x803498DBu);
        gfxlib::draw_polygon(b, xs, ys, n, 0xFF3498DBu);
    }
}

void Chart::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    switch (type_) {
        case ChartLine:  draw_line_chart(b, ox, oy); break;
        case ChartBar:   draw_bar_chart(b, ox, oy); break;
        case ChartPie:   draw_pie_chart(b, ox, oy); break;
        case ChartArea:  draw_area_chart(b, ox, oy); break;
        case ChartScatter: draw_scatter(b, ox, oy); break;
        case ChartRadar: draw_radar(b, ox, oy); break;
    }
    // 图例
    int lx = ox + 4, ly = oy + h - 14;
    for (int s = 0; s < names_.size(); s++) {
        gfxlib::draw_rect_fill(b, lx, ly, 8, 8, colors_[s]);
        draw_text(b, lx + 12, ly, names_[s].c_str(), t.text_dim);
        lx += 12 + text_width(names_[s].c_str()) + 10;
    }
}

// ===================== 模块自检 =====================
int chart_self_test() {
    int fail = 0;

    // --- 数据点 -> 像素映射 ---
    {
        Chart c;
        c.set_bounds(0, 0, 200, 120);
        c.set_y_range(0, 100);
        // plot_top=10, plot_bottom=h-24=96
        // value 0  -> y=96; value 100 -> y=10; value 50 -> 53
        int y0 = c.value_to_px(0);
        int y100 = c.value_to_px(100);
        int y50 = c.value_to_px(50);
        if (y0 != 96) fail++;
        if (y100 != 10) fail++;
        // 中间值应在 10 与 96 之间,约 53
        if (y50 <= 10 || y50 >= 96) fail++;
        // 越界钳制
        if (c.value_to_px(-10) != 96) fail++;
        if (c.value_to_px(200) != 10) fail++;
    }

    // --- index_to_px:点均匀分布 ---
    {
        Chart c;
        c.set_bounds(0, 0, 200, 120);
        // plot_left=30, plot_right=190
        int x0 = c.index_to_px(0, 4);
        int x3 = c.index_to_px(3, 4);
        // 应落在 30..190 内且递增
        if (x0 <= 30) fail++;
        if (x3 >= 190) fail++;
        if (x3 <= x0) fail++;
    }

    // --- 折线数据追加 ---
    {
        Chart c;
        int s = c.add_series("S1", 0xFF3498DBu);
        c.add_value(s, 10);
        c.add_value(s, 20);
        c.add_value(s, 30);
        if (c.point_count(s) != 3) fail++;
        if (c.series_count() != 1) fail++;
    }

    // --- 散点 ---
    {
        Chart c;
        c.set_type(ChartScatter);
        int s = c.add_series("pts", 0xFFE74C3Cu);
        c.add_point(s, 10, 20);
        c.add_point(s, 30, 40);
        if (c.point_count(s) != 0) fail++;   // 散点存在 points_ 而非 values_
    }

    // --- 实时滚动 ---
    {
        Chart c;
        c.set_type(ChartLine);
        for (int i = 0; i < 100; i++) c.push_realtime((double)i);
        // realtime_max_=50,应只保留 50 个点
        if (c.point_count(0) != 50) fail++;
    }

    // --- 饼图数据总和 ---
    {
        Chart c;
        c.set_type(ChartPie);
        int s = c.add_series("pie", 0);
        c.add_value(s, 30);
        c.add_value(s, 10);
        c.add_value(s, 60);
        if (c.point_count(s) != 3) fail++;
    }

    // --- 类型切换 ---
    {
        Chart c;
        c.set_type(ChartBar);
        if (c.type() != ChartBar) fail++;
    }

    // --- 多系列管理 ---
    {
        Chart c;
        int a = c.add_series("A", 0xFF111111u);
        int b = c.add_series("B", 0xFF222222u);
        if (c.series_count() != 2) fail++;
        c.add_value(a, 1); c.add_value(a, 2);
        c.add_value(b, 9); c.add_value(b, 8); c.add_value(b, 7);
        if (c.point_count(a) != 2) fail++;
        if (c.point_count(b) != 3) fail++;
        c.clear_series(a);
        if (c.point_count(a) != 0) fail++;
    }

    // --- y 范围默认值 ---
    {
        Chart c;
        if (c.y_min() != 0 || c.y_max() != 100) fail++;
        c.set_y_range(-50, 50);
        if (c.y_min() != -50 || c.y_max() != 50) fail++;
    }

    // --- 散点坐标映射(自定义范围) ---
    {
        Chart c;
        c.set_bounds(0, 0, 200, 120);
        c.set_y_range(-50, 50);
        // y=-50 应映射到 plot_bottom=96;y=50 到 plot_top=10
        if (c.value_to_px(-50) != 96) fail++;
        if (c.value_to_px(50) != 10) fail++;
        if (c.value_to_px(0) <= 10 || c.value_to_px(0) >= 96) fail++;
    }

    // --- 面积/雷达渲染不崩溃 ---
    {
        Chart c;
        gfxlib::Pixel* fake = 0;
        (void)fake;
        // 用一个真实小 Buffer 渲染各类型,确保不越界崩溃
        uint32_t* px = new uint32_t[64 * 48];
        gfxlib::Buffer b = { px, 64, 48 };
        for (int t = ChartLine; t <= ChartRadar; t = (ChartType)(t + 1)) {
            c.set_type((ChartType)t);
            int s = c.add_series("x", 0xFF3498DBu);
            for (int i = 0; i < 5; i++) c.add_value(s, (double)(i * 10));
            c.set_bounds(0, 0, 64, 48);
            c.on_paint(b, 0, 0);
            c.clear_series(s);
        }
        delete[] px;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
