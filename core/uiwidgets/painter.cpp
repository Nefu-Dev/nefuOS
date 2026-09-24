// nefuOS UI 组件库 —— Painter 实现
#include "painter.h"
#include "widget.h"
#include <string.h>
#include <stdlib.h>
#include <cstdio>

namespace nefu {
namespace ui {

// 对外暴露 widget.cpp 内的 5x7 字体
void ux_render_text(gfxlib::Buffer b, int x, int y, const char* s, gfxlib::Pixel fg) {
    // 借一个临时 Widget 实例调用其字体绘制逻辑(无副作用)
    Widget w;
    w.draw_text(b, x, y, s, fg);
}
int ux_text_width(const char* s) {
    Widget w;
    return w.text_width(s);
}

// 取/拆通道
static inline int chan(gfxlib::Pixel p, int shift) { return (p >> shift) & 0xFF; }

void Painter::fill_rect(int x, int y, int w, int h, gfxlib::Pixel c) {
    // 与裁剪框求交
    int x0 = x > clip_.x ? x : clip_.x;
    int y0 = y > clip_.y ? y : clip_.y;
    int x1 = (x + w) < (clip_.x + clip_.w) ? (x + w) : (clip_.x + clip_.w);
    int y1 = (y + h) < (clip_.y + clip_.h) ? (y + h) : (clip_.y + clip_.h);
    if (x1 <= x0 || y1 <= y0) return;
    gfxlib::draw_rect_fill(buf_, x0, y0, x1 - x0, y1 - y0, c);
}
void Painter::frame_rect(int x, int y, int w, int h, gfxlib::Pixel c) {
    if (!inside_clip(x, y) && !inside_clip(x + w - 1, y + h - 1)) return;
    gfxlib::draw_rect(buf_, x, y, w, h, c);
}

// 圆角矩形:填充中心块 + 四条边 + 四角近似(小圆用圆/线近似)
void Painter::fill_round_rect(int x, int y, int w, int h, int r, gfxlib::Pixel c) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    // 中间主体
    fill_rect(x + r, y, w - 2 * r, h, c);
    // 左右直边
    fill_rect(x, y + r, r, h - 2 * r, c);
    fill_rect(x + w - r, y + r, r, h - 2 * r, c);
    // 四角:用填充圆近似象限
    circle_fill(x + r, y + r, r, c);
    circle_fill(x + w - r - 1, y + r, r, c);
    circle_fill(x + r, y + h - r - 1, r, c);
    circle_fill(x + w - r - 1, y + h - r - 1, r, c);
}
void Painter::frame_round_rect(int x, int y, int w, int h, int r, gfxlib::Pixel c) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    line(x + r, y, x + w - r, y, c);
    line(x + r, y + h - 1, x + w - r, y + h - 1, c);
    line(x, y + r, x, y + h - r, c);
    line(x + w - 1, y + r, x + w - 1, y + h - r, c);
    circle(x + r, y + r, r, c);
    circle(x + w - r - 1, y + r, r, c);
    circle(x + r, y + h - r - 1, r, c);
    circle(x + w - r - 1, y + h - r - 1, r, c);
}

// 垂直渐变:按行在两色间线性插值 RGB
void Painter::fill_vgradient(int x, int y, int w, int h, gfxlib::Pixel top, gfxlib::Pixel bot) {
    if (h <= 0) return;
    int tr = chan(top, 16), tg = chan(top, 8), tb = chan(top, 0);
    int br = chan(bot, 16), bg = chan(bot, 8), bb = chan(bot, 0);
    for (int i = 0; i < h; i++) {
        int t = (h == 1) ? 0 : i * 255 / (h - 1);
        int r = tr + (br - tr) * t / 255;
        int g = tg + (bg - tg) * t / 255;
        int b = tb + (bb - tb) * t / 255;
        gfxlib::Pixel c = (0xFFu << 24) | ((gfxlib::Pixel)r << 16) |
                          ((gfxlib::Pixel)g << 8) | (gfxlib::Pixel)b;
        fill_rect(x, y + i, w, 1, c);
    }
}
void Painter::fill_hgradient(int x, int y, int w, int h, gfxlib::Pixel left, gfxlib::Pixel right) {
    if (w <= 0) return;
    int lr = chan(left, 16), lg = chan(left, 8), lb = chan(left, 0);
    int rr = chan(right, 16), rg = chan(right, 8), rb = chan(right, 0);
    for (int i = 0; i < w; i++) {
        int t = (w == 1) ? 0 : i * 255 / (w - 1);
        int r = lr + (rr - lr) * t / 255;
        int g = lg + (rg - lg) * t / 255;
        int b = lb + (rb - lb) * t / 255;
        gfxlib::Pixel c = (0xFFu << 24) | ((gfxlib::Pixel)r << 16) |
                          ((gfxlib::Pixel)g << 8) | (gfxlib::Pixel)b;
        fill_rect(x + i, y, 1, h, c);
    }
}

// 裁剪文字:逐字符绘制,遇到裁剪/最大宽度边界即停,返回实际绘制字符数
int Painter::draw_text_clipped(int x, int y, const char* s, gfxlib::Pixel fg, int max_w) {
    int drawn = 0;
    int cx = x;
    int limit = x + max_w;
    for (const char* p = s; *p; p++) {
        if (cx + 6 > limit) break;                 // 超过最大宽度
        if (cx < clip_.x || cx + 6 > clip_.x + clip_.w) break;  // 越出水平裁剪
        if (y < clip_.y || y + 7 > clip_.y + clip_.h) break;    // 越出垂直裁剪
        // 单字符绘制
        char one[2] = { *p, 0 };
        ux_render_text(buf_, cx, y, one, fg);
        cx += 6;
        drawn++;
    }
    return drawn;
}

// ===================== 模块自检 =====================
int painter_self_test() {
    int fail = 0;
    // 用一块栈上小 Buffer 验证图元不越界 + 几何逻辑
    uint32_t px[32 * 32];
    for (int i = 0; i < 32 * 32; i++) px[i] = 0;
    gfxlib::Buffer b = { px, 32, 32 };
    Painter p(b);

    // 基础填充
    p.fill_rect(0, 0, 32, 32, 0xFFFFFFFFu);
    if (px[0] != 0xFFFFFFFFu) fail++;
    // 裁剪:超出右边界的矩形不应改变任何像素
    uint32_t before = px[31];
    p.fill_rect(100, 0, 50, 10, 0xFF000000u);
    if (px[31] != before) fail++;
    // 垂直渐变:两端颜色应等于给定端点
    p.fill_vgradient(0, 0, 8, 8, 0xFF000000u, 0xFFFFFFFFu);
    gfxlib::Pixel top_row = px[0];
    if (chan(top_row, 16) != 0 || chan(top_row, 8) != 0 || chan(top_row, 0) != 0) fail++;
    gfxlib::Pixel bot_row = px[7 * 32];
    if (chan(bot_row, 16) != 255 || chan(bot_row, 8) != 255 || chan(bot_row, 0) != 255) fail++;

    // 圆角矩形不崩溃
    p.fill_round_rect(2, 2, 20, 12, 4, 0xFF333333u);
    p.frame_round_rect(2, 2, 20, 12, 4, 0xFFFFFFFFu);

    // 水平渐变
    p.fill_hgradient(0, 20, 16, 4, 0xFFFF0000u, 0xFF0000FFu);
    gfxlib::Pixel lc = px[20 * 32 + 0];
    if (chan(lc, 16) != 255 || chan(lc, 0) != 0) fail++;
    gfxlib::Pixel rc = px[20 * 32 + 15];
    if (chan(rc, 16) != 0 || chan(rc, 0) != 255) fail++;

    // 裁剪文字:max_w=12px 最多放 2 个字符(6px/字)
    p.reset_clip();
    int n = p.draw_text_clipped(0, 24, "ABCD", 0xFF000000u, 12);
    if (n != 2) fail++;
    // 裁剪宽度 6,只放得下 1 个字符
    p.set_clip(0, 0, 6, 32);
    int n2 = p.draw_text_clipped(0, 24, "ABCD", 0xFF000000u, 100);
    if (n2 != 1) fail++;

    return fail;
}

} // namespace ui
} // namespace nefu
