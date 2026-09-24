// ============================================================================
// nefuOS 光线追踪引擎 —— rtlighttree 实现
// ============================================================================
#include "rtlighttree.h"

namespace nefu {
namespace raytrace {

void RTLightTree::build(const Light* lights, int n) {
    delete[] nodes;
    node_cap = n * 2;
    nodes = new RTLightNode[node_cap];
    node_count = 0;
    // 简化：所有光源放一个根节点（真实实现递归分桶）
    RTLightNode root;
    root.box = RTAABB();
    for (int i = 0; i < n; i++) {
        RTAABB lb;
        lb.expand(lights[i].pos);
        root.box.expand(lb.mn);
        root.box.expand(lb.mx);
    }
    root.first = 0;
    root.count = n;
    nodes[node_count++] = root;
}

RTVec3 RTLightTree::query(const RTVec3& p, const Light* lights, int n) const {
    if (!nodes || n <= 0) return RTVec3(0,0,0);
    RTVec3 sum(0,0,0);
    for (int i = 0; i < n; i++) {
        RTVec3 d = lights[i].pos - p;
        rtfx dist2 = d.length_sq();
        if (dist2 <= 0) continue;
        RTVec3 col = lights[i].color;
        sum += col * rt_div(RT_ONE, rt_itofx(1) + dist2);
    }
    return sum;
}

int rtlighttree_self_test() {
    int fail = 0;
    Light ls[3];
    ls[0] = Light::point(RTVec3(0,rt_itofx(3),0), RTVec3(RT_ONE,0,0), rt_itofx(1));
    ls[1] = Light::point(RTVec3(rt_itofx(3),rt_itofx(3),0), RTVec3(0,RT_ONE,0), rt_itofx(1));
    ls[2] = Light::point(RTVec3(-rt_itofx(3),rt_itofx(3),0), RTVec3(0,0,RT_ONE), rt_itofx(1));
    RTLightTree t;
    t.build(ls, 3);
    // 1. 查询有贡献
    {
        RTVec3 c = t.query(RTVec3(0,0,0), ls, 3);
        if (c.x <= 0 && c.y <= 0 && c.z <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
