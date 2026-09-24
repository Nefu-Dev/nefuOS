// ============================================================================
// nefuOS 光线追踪引擎 —— rtlightprobe: 光照探针
// ----------------------------------------------------------------------------
// 在空间中散布探针，预计算各方向辐照度：
//   - 每个探针存 6 个方向（立方体面）的颜色
//   - 运行时按最近探针插值
//   - 用于快速全局光照近似
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTProbe {
    RTVec3 pos;
    RTVec3 faces[6];   // +X -X +Y -Y +Z -Z

    RTProbe() { for (int i = 0; i < 6; i++) faces[i] = RTVec3(0,0,0); }
};

struct RTSkyProbes {
    RTProbe* probes;
    int count;
    RTSkyProbes() : probes(0), count(0) {}
    ~RTSkyProbes() { delete[] probes; }

    void alloc(int n);
    // 在位置 p 处插值辐照度
    RTVec3 sample(const RTVec3& p, const RTVec3& dir) const;
    // 用环境色填充所有探针
    void fill_solid(const RTVec3& c);
};

int rtlightprobe_self_test();

} // namespace raytrace
} // namespace nefu
