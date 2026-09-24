#include <cstdio>
#include <cmath>
#include "simlib/sim_all.h"

using namespace nefu::simx;

int main() {
    // perlin 数值
    {
        PerlinNoise pn(42);
        for (int i = 0; i < 5; i++)
            printf("noise(%f,%f)=%f\n", (double)i * 0.7, (double)i * 0.3, pn.noise((double)i * 0.7, (double)i * 0.3));
        double mn = 9, mx = -9;
        for (int i = 0; i < 5000; i++) {
            double v = pn.noise((double)(i % 200) * 0.13, (double)(i % 150) * 0.11);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        printf("perlin sweep: min=%f max=%f\n", mn, mx);
        // 整数晶格点噪声（应呈现渐变性质）
        for (int ix = 0; ix < 3; ix++)
            for (int iy = 0; iy < 3; iy++)
                printf("  lattice(%d,%d)=%f\n", ix, iy, pn.noise((double)ix, (double)iy));
    }
    // boids sep 数值
    {
        Boids b(500, 500);
        b.add(250, 250, 0, 0);
        b.add(260, 250, 0, 0);
        for (int s = 0; s < 4; s++) {
            b.step();
            double d = std::sqrt((b.bird(0).x - b.bird(1).x) * (b.bird(0).x - b.bird(1).x) +
                                 (b.bird(0).y - b.bird(1).y) * (b.bird(0).y - b.bird(1).y));
            printf("sep step%d: d=%f v0=(%f,%f) v1=(%f,%f)\n", s, d,
                   b.bird(0).vx, b.bird(0).vy, b.bird(1).vx, b.bird(1).vy);
        }
    }
    // epidemic 恢复
    {
        Epidemic e(500, 20);
        e.set_params(0.0, 0.2);
        for (int i = 0; i < 30; i++) {
            e.step_day();
            if (i % 5 == 4) printf("epid day%d: I=%d R=%d\n", i + 1, e.state().I, e.state().R);
        }
    }
    return 0;
}
