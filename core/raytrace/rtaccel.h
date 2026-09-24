// ============================================================================
// nefuOS 光线追踪引擎 —— rtaccel: SAH 分箱加速结构
// ----------------------------------------------------------------------------
// 相比 bvh.cpp 的中位数划分，这里实现更精确的表面积启发式（SAH）：
//   - 16 个桶（bucket）扫描质心，逐桶计算分裂代价
//   - 选择代价最小的分裂面
//   - 叶子图元数上限 4
// 用于大规模场景，求交更平衡。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"
#include "../klib/klib.h"

namespace nefu {
namespace raytrace {

struct AccelNode {
    RTAABB box;
    int    left;       // 左孩子（叶子=-1）
    int    first;      // 叶子：图元区间起点
    int    count;      // 叶子：图元数
};

struct RTAccel {
    nefu::List<AccelNode> nodes;
    nefu::List<int>       ordered;
    const Primitive* prims;
    int count;

    RTAccel() : prims(0), count(0) {}

    void build(const Primitive* p, int n);
    bool hit(const RTRay& ray, HitInfo& h) const;
    int  node_count() const { return nodes.size(); }

private:
    int build_recurse(int* idx, int lo, int hi, const RTAABB* all_box);
};

int rtaccel_self_test();

} // namespace raytrace
} // namespace nefu
