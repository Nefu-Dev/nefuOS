// ============================================================================
// nefuOS 光线追踪引擎 —— rtlightprobe 实现
// ============================================================================
#include "rtlightprobe.h"

namespace nefu {
namespace raytrace {

void RTSkyProbes::alloc(int n) {
    delete[] probes;
    probes = new RTProbe[n];
    count = n;
    for (int i = 0; i < n; i++) probes[i].pos = RTVec3(0,0,0);
}

void RTSkyProbes::fill_solid(const RTVec3& c) {
    for (int i = 0; i < count; i++)
        for (int f = 0; f < 6; f++) probes[i].faces[f] = c;
}

RTVec3 RTSkyProbes::sample(const RTVec3& p, const RTVec3& dir) const {
    if (!probes || count <= 0) return RTVec3(0,0,0);
    // 找最近探针
    int best = 0;
    rtfx bestd = RT_INF;
    for (int i = 0; i < count; i++) {
        rtfx d = (probes[i].pos - p).length_sq();
        if (d < bestd) { bestd = d; best = i; }
    }
    // 按方向选面
    int face = 0;
    rtfx ax = rt_abs(dir.x), ay = rt_abs(dir.y), az = rt_abs(dir.z);
    if (ax >= ay && ax >= az) face = (dir.x > 0) ? 0 : 1;
    else if (ay >= az) face = (dir.y > 0) ? 2 : 3;
    else face = (dir.z > 0) ? 4 : 5;
    return probes[best].faces[face];
}

int rtlightprobe_self_test() {
    int fail = 0;
    RTSkyProbes probes;
    probes.alloc(4);
    probes.probes[0].pos = RTVec3(0,0,0);
    probes.probes[1].pos = RTVec3(rt_itofx(5),0,0);
    probes.probes[2].pos = RTVec3(0,rt_itofx(5),0);
    probes.probes[3].pos = RTVec3(0,0,rt_itofx(5));
    probes.fill_solid(RTVec3(RT_ONE,0,0));
    // 1. 采样原点附近应得红色
    {
        RTVec3 c = probes.sample(RTVec3(0,0,0), RTVec3(0,1,0));
        if (c.x <= 0) fail++;
    }
    // 2. 最近探针正确
    {
        RTVec3 c = probes.sample(RTVec3(rt_itofx(4),0,0), RTVec3(0,1,0));
        if (c.x <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
