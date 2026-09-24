// bisect which raster call blows the stack
#include "gfxlib/raster.h"
#include <cstdio>

int main() {
    nefu::gfxlib::Pixel buf[16 * 16];
    nefu::gfxlib::Buffer b;
    b.data = buf; b.w = 16; b.h = 16;
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;

    std::printf("line\n"); std::fflush(stdout);
    nefu::gfxlib::draw_line(b, 0, 8, 15, 8, 0xFFFFFFFF);

    std::printf("circle\n"); std::fflush(stdout);
    nefu::gfxlib::draw_circle(b, 7, 7, 5, 0xFFFFFFFF);

    std::printf("circle_fill\n"); std::fflush(stdout);
    nefu::gfxlib::draw_circle_fill(b, 7, 7, 4, 0xFFFFFFFF);

    std::printf("ellipse\n"); std::fflush(stdout);
    nefu::gfxlib::draw_ellipse(b, 7, 7, 5, 3, 0xFFFFFFFF);

    std::printf("bezier\n"); std::fflush(stdout);
    nefu::gfxlib::draw_bezier(b, 0, 0, 5, 14, 10, 14, 15, 0, 0xFFFFFFFF);

    std::printf("polygon\n"); std::fflush(stdout);
    int xs[3] = {2, 14, 8}, ys[3] = {14, 14, 2};
    nefu::gfxlib::fill_polygon(b, xs, ys, 3, 0xFFFFFFFF);

    std::printf("flood\n"); std::fflush(stdout);
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0xFFFF0000;
    nefu::gfxlib::draw_rect(b, 4, 4, 8, 8, 0xFF000000);
    nefu::gfxlib::flood_fill(b, 6, 6, 0xFFFF0000, 0xFF00FF00);

    std::printf("clip_line\n"); std::fflush(stdout);
    int x0 = -10, y0 = 5, x1 = 20, y1 = 5;
    nefu::gfxlib::clip_line_cohen(x0, y0, x1, y1, 0, 0, 15, 15);

    std::printf("clip_poly\n"); std::fflush(stdout);
    int xs2[4] = {-4, 8, 8, -4}, ys2[4] = {2, 2, 14, 14};
    int ox[64], oy[64];
    nefu::gfxlib::clip_polygon_sutherland(xs2, ys2, 4, 0, 0, 15, 15, ox, oy, 64);

    std::printf("ALL DONE\n");
    return 0;
}
