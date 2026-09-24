#include "gfxlib/raster.h"
#include <cstdio>

int main() {
    nefu::gfxlib::Pixel buf[16 * 16];
    nefu::gfxlib::Buffer b;
    b.data = buf; b.w = 16; b.h = 16;
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    std::printf("pre\n"); std::fflush(stdout);
    int xs[3] = {2, 14, 8}, ys[3] = {14, 14, 2};
    nefu::gfxlib::fill_polygon(b, xs, ys, 3, 0xFFFFFFFF);
    std::printf("post center=%08x\n", (unsigned)buf[10 * 16 + 8]);
    return 0;
}
