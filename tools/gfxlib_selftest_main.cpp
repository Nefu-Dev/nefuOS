// gfxlib standalone self-test host (dev tool, not part of nefuOS)
#include "gfxlib/gfxlib_all.h"
#include <cstdio>

int main() {
    std::printf("raster...\n"); std::fflush(stdout);
    int rf = nefu::gfxlib::raster_self_test();
    std::printf("raster=%d\n", rf); std::fflush(stdout);
    std::printf("geo...\n"); std::fflush(stdout);
    int gf = nefu::gfxlib::geo_self_test();
    std::printf("geo=%d\n", gf); std::fflush(stdout);
    std::printf("transform...\n"); std::fflush(stdout);
    int tf = nefu::gfxlib::transform_self_test();
    std::printf("transform=%d\n", tf); std::fflush(stdout);
    std::printf("noise...\n"); std::fflush(stdout);
    int nf = nefu::gfxlib::noise_self_test();
    std::printf("noise=%d\n", nf); std::fflush(stdout);
    std::printf("color...\n"); std::fflush(stdout);
    int cf = nefu::gfxlib::color_self_test();
    std::printf("color=%d\n", cf); std::fflush(stdout);
    std::printf("TOTAL=%d\n", rf + gf + tf + nf + cf);
    return 0;
}
