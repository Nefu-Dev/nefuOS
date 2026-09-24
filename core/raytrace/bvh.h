// ============================================================================
// nefuOS 光线追踪引擎 —— bvh: 层次包围盒加速结构
// ----------------------------------------------------------------------------
// 对场景图元列表构建 SAH 引导的 BVH：
//   - 自顶向下按最长轴中位数划分（表面积启发式近似）
//   - 叶子节点存图元索引区间
//   - 射线遍历用显式栈（无递归），先粗剪包围盒再做精确求交
// 目的：把 O(n) 求交降到 O(log n)。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"
#include "../klib/klib.h"

namespace nefu {
namespace raytrace {

struct BVHNode {
    RTAABB box;
    int    left;        // 左子节点下标（叶子=-1）
    int    first;       // 叶子：首个图元下标
    int    count;       // 叶子：图元数（0=内部节点）
};

struct BVH {
    nefu::List<BVHNode> nodes;
    nefu::List<int>     ordered;   // 重排后的图元索引
    const Primitive* prims;
    int count;

    BVH() : prims(0), count(0) {}

    // 构建：prims 数组必须在 BVH 存活期间保持有效
    void build(const Primitive* prim_array, int n);

    // 射线遍历：返回最近命中，填 HitInfo（prim_id 为图元下标）
    bool hit(const RTRay& ray, HitInfo& h) const;

private:
    // 递归构建节点，返回节点下标
    int build_recurse(int* indices, int lo, int hi, RTAABB* centroid_box);
};

int bvh_self_test();

} // namespace raytrace
} // namespace nefu
