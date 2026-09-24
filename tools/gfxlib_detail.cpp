#include "gfxlib/transform.h"
#include "gfxlib/noise.h"
#include "gfxlib/color.h"
#include <cstdio>
using namespace nefu::fx;
using namespace nefu::gfxlib;

static void pr(const char* n, fix v) { std::printf("%s=%d (%.4f)\n", n, v, (double)v / 65536.0); }

int main() {
    // transform details
    fix ox, oy;
    Mat2x3 rot = mat_rotate(nefu::fx::FX_PI_2);
    mat_transform(rot, itofix(1), 0, &ox, &oy);
    pr("rot-x", ox); pr("rot-y", oy);
    Mat2x3 comb = mat_mul(mat_rotate(nefu::fx::FX_PI_2), mat_translate(itofix(3), 0));
    mat_transform(comb, 0, 0, &ox, &oy);
    pr("comb-x", ox); pr("comb-y", oy);
    Mat2x3 fwd = mat_mul(mat_translate(itofix(5), itofix(-2)), mat_scale(itofix(2), itofix(3)));
    Mat2x3 inv; bool iok = mat_inverse(fwd, &inv);
    Mat2x3 back = mat_mul(inv, fwd);
    mat_transform(back, itofix(11), itofix(-7), &ox, &oy);
    pr("inv-ok", iok ? 1 : 0); pr("inv-x", ox); pr("inv-y", oy);
    Mat3 rx = mat3_rotate_x(nefu::fx::FX_PI_2);
    Vec3 uz = {0, 0, itofix(1)};
    Vec3 rz = mat3_apply(rx, uz);
    pr("rz-x", rz.x); pr("rz-y", rz.y); pr("rz-z", rz.z);
    Vec3 cpt = {0, 0, itofix(-10)};
    int sx, sy;
    bool pc = project(cpt, itofix(10), 800, 600, &sx, &sy);
    std::printf("proj-center ok=%d sx=%d sy=%d\n", pc, sx, sy);
    Vec3 right = {itofix(10), 0, itofix(-10)};
    pc = project(right, itofix(10), 800, 600, &sx, &sy);
    std::printf("proj-right ok=%d sx=%d sy=%d\n", pc, sx, sy);

    // noise details
    int e = mandelbrot_escape(0, fx_div(itofix(15), itofix(10)), 64);
    std::printf("mandel(0,1.5)=%d\n", e);
    e = mandelbrot_escape(0, itofix(1), 64);
    std::printf("mandel(0,1)=%d\n", e);
    fix fv = value_noise2d_smooth(0, 0, 1);
    pr("value-noise(0,0)", fv);
    fv = fbm2d(itofix(0), itofix(0), 4, itofix(2), fxf(5,10), 0x1234ABCDu);
    pr("fbm(0,0)", fv);
    fix jr = julia_escape(0, 0, fxf(-8,10), fxf(16,10), 64);
    pr("julia(0,0)", jr);
    fix w = worley2d(0, 0, 0x1234ABCDu);
    pr("worley(0,0)", w);
    fix l1 = logistic_map(fx_div(itofix(2), itofix(10)), itofix(4));
    pr("logistic(0.2,4)", l1);
    LSystem ls;
    ls.axiom[0]='A'; ls.axiom[1]=0;
    ls.ruleA[0]='A'; ls.ruleA[1]='+'; ls.ruleA[2]='A'; ls.ruleA[3]='-'; ls.ruleA[4]='-'; ls.ruleA[5]='A'; ls.ruleA[6]='+'; ls.ruleA[7]='A'; ls.ruleA[8]=0;
    ls.ruleB[0]=0;
    ls.angle = fx_div(FX_PI, itofix(3));
    ls.step = itofix(1);
    ls.expand(1);
    std::printf("ls1 len=%d out='%s'\n", ls.out_len, ls.output);
    ls.expand(3);
    std::printf("ls3 len=%d\n", ls.out_len);

    // color details
    HSL h = rgb_to_hsl(rgb(255, 0, 0));
    std::printf("hsl red h=%d s=%d l=%d\n", h.h, h.s, h.l);
    YUV y = rgb_to_yuv(rgb(10, 200, 30));
    std::printf("yuv y=%d u=%d v=%d\n", y.y, y.u, y.v);
    RGB back2 = yuv_to_rgb(y);
    std::printf("yuv back r=%d g=%d b=%d\n", back2.r, back2.g, back2.b);
    RGB g1 = rgb_gamma(rgb(128,128,128), 200);
    std::printf("gamma200=%d,%d,%d\n", g1.r, g1.g, g1.b);
    CMYK k = rgb_to_cmyk(rgb(255,0,0));
    std::printf("cmyk red c=%d m=%d y=%d k=%d\n", k.c, k.m, k.y, k.k);
    return 0;
}
