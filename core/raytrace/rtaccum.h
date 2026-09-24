// ============================================================================
// nefuOS 光线追踪引擎 —— rtaccum: 渐进渲染像素累积器
// ----------------------------------------------------------------------------
// 逐帧累加采样：
//   - sample_count++ 后平均
//   - 每帧只需追加，不用重算
//   - 支持方差估计（收敛判断）
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTAccum {
    RTVec3* sum;
    int*    count;
    int     w, h;
    RTAccum() : sum(0), count(0), w(0), h(0) {}
    ~RTAccum() { delete[] sum; delete[] count; }

    void alloc(int width, int height);
    void add(int x, int y, const RTVec3& c);
    RTVec3 avg(int x, int y) const;
    int    samples(int x, int y) const;
    void reset();
};

int rtaccum_self_test();

} // namespace raytrace
} // namespace nefu
