// nefuOS UI 组件库 —— Painter 2D 绘制辅助
// 在 gfxlib::Buffer 之上提供:圆角矩形、垂直/水平渐变、裁剪文字、alpha 混合。
// 纯整数运算,无 STL/异常/RTTI。
#pragma once

#include "gfxlib/gfxlib_all.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

// 全局文字绘制/测量(由 widget.cpp 的 5x7 字体实现,Painter 复用)
void ux_render_text(gfxlib::Buffer b, int x, int y, const char* s, gfxlib::Pixel fg);
int  ux_text_width(const char* s);

// 2D 绘制器:绑定一块离屏 Buffer,提供高层图元
class Painter {
public:
    explicit Painter(gfxlib::Buffer b) : buf_(b), clip_({ 0, 0, b.w, b.h }) {}

    // ---- 裁剪 ----
    void set_clip(int x, int y, int w, int h) { clip_ = { x, y, w, h }; }
    void reset_clip() { clip_ = { 0, 0, buf_.w, buf_.h }; }
    bool inside_clip(int x, int y) const {
        return x >= clip_.x && x < clip_.x + clip_.w &&
               y >= clip_.y && y < clip_.y + clip_.h;
    }

    // ---- 基础矩形(带裁剪) ----
    void fill_rect(int x, int y, int w, int h, gfxlib::Pixel c);
    void frame_rect(int x, int y, int w, int h, gfxlib::Pixel c);

    // ---- 圆角矩形 ----
    void fill_round_rect(int x, int y, int w, int h, int r, gfxlib::Pixel c);
    void frame_round_rect(int x, int y, int w, int h, int r, gfxlib::Pixel c);

    // ---- 渐变 ----
    void fill_vgradient(int x, int y, int w, int h, gfxlib::Pixel top, gfxlib::Pixel bot);
    void fill_hgradient(int x, int y, int w, int h, gfxlib::Pixel left, gfxlib::Pixel right);

    // ---- 圆 / 线 ----
    void circle(int cx, int cy, int r, gfxlib::Pixel c) {
        gfxlib::draw_circle(buf_, cx, cy, r, c);
    }
    void circle_fill(int cx, int cy, int r, gfxlib::Pixel c) {
        gfxlib::draw_circle_fill(buf_, cx, cy, r, c);
    }
    void line(int x0, int y0, int x1, int y1, gfxlib::Pixel c) {
        gfxlib::draw_line(buf_, x0, y0, x1, y1, c);
    }

    // ---- 裁剪文字(超出 clipping 区域不画) ----
    int draw_text_clipped(int x, int y, const char* s, gfxlib::Pixel fg, int max_w);

    int width() const { return buf_.w; }
    int height() const { return buf_.h; }

private:
    gfxlib::Buffer buf_;
    struct { int x, y, w, h; } clip_;
};

// ===================== 模块自检 =====================
int painter_self_test();

} // namespace ui
} // namespace nefu
