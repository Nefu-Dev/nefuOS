// ============================================================================
// nefuOS 光线追踪引擎 —— rtaccum 实现
// ============================================================================
#include "rtaccum.h"

namespace nefu {
namespace raytrace {

void RTAccum::alloc(int width, int height) {
    delete[] sum; delete[] count;
    w = width; h = height;
    sum = new RTVec3[w * h];
    count = new int[w * h];
    reset();
}

void RTAccum::reset() {
    if (!sum) return;
    for (int i = 0; i < w*h; i++) { sum[i] = RTVec3(0,0,0); count[i] = 0; }
}

void RTAccum::add(int x, int y, const RTVec3& c) {
    if (x<0||x>=w||y<0||y>=h) return;
    int i = y*w+x;
    sum[i] = sum[i] + c;
    count[i]++;
}

RTVec3 RTAccum::avg(int x, int y) const {
    if (x<0||x>=w||y<0||y>=h) return RTVec3(0,0,0);
    int i = y*w+x;
    if (count[i] <= 0) return RTVec3(0,0,0);
    return sum[i] / rt_itofx(count[i]);
}

int RTAccum::samples(int x, int y) const {
    if (x<0||x>=w||y<0||y>=h) return 0;
    return count[y*w+x];
}

int rtaccum_self_test() {
    int fail = 0;
    RTAccum a;
    a.alloc(4,4);
    a.add(0,0, RTVec3(RT_ONE,0,0));
    a.add(0,0, RTVec3(RT_ONE,0,0));
    // 1. 平均 = 1
    {
        RTVec3 c = a.avg(0,0);
        if (!rt_near(c.x, RT_ONE, 400)) fail++;
    }
    // 2. 采样数 = 2
    {
        if (a.samples(0,0) != 2) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
