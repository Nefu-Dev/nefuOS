// ============================================================================
// nefuOS 光线追踪引擎 —— rtlighttree: 光源层次包围树
// ----------------------------------------------------------------------------
// 把多个面光/点光组织成 BVH，加速 NEE：
//   - 按光源包围盒分桶
//   - 遍历跳过不照亮阴影点的光源
//   - 大量光源时显著加速
// ============================================================================
#pragma once
#include "rtmath.h"
#include "lights.h"

namespace nefu {
namespace raytrace {

struct RTLightNode {
    RTAABB box;
    int    first;    // 叶节点：光源下标；内部节点：左孩子
    int    count;    // 叶节点：光源数；内部节点：右孩子
};

struct RTLightTree {
    RTLightNode* nodes;
    int          node_count;
    int          node_cap;
    RTLightTree() : nodes(0), node_count(0), node_cap(0) {}
    ~RTLightTree() { delete[] nodes; }

    void build(const Light* lights, int n);
    // 查询从 p 出发朝 dir 方向最近的光源贡献
    RTVec3 query(const RTVec3& p, const Light* lights, int n) const;
};

int rtlighttree_self_test();

} // namespace raytrace
} // namespace nefu
