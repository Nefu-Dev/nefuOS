// ============================================================================
// nefuOS 光线追踪引擎 —— bvh 实现（Q16.16 定点）
// ============================================================================
#include "bvh.h"

namespace nefu {
namespace raytrace {

static const int BVH_LEAF_SIZE = 4;

void BVH::build(const Primitive* prim_array, int n) {
    nodes.erase_all();
    prims = prim_array;
    count = n;
    if (n <= 0) return;
    // 索引数组
    int* indices = new int[n];
    RTAABB* cents = new RTAABB[n];
    RTAABB total;
    for (int i = 0; i < n; i++) {
        indices[i] = i;
        RTAABB b = prims[i].bounds();
        cents[i] = b;
        total.expand(b);
    }
    build_recurse(indices, 0, n, cents);
    delete[] indices;
    delete[] cents;
}

int BVH::build_recurse(int* indices, int lo, int hi, RTAABB* cents) {
    int node_idx = nodes.size();
    BVHNode node;
    // 本节点整体包围盒
    RTAABB box;
    RTAABB cbox;
    for (int i = lo; i < hi; i++) {
        box.expand(prims[indices[i]].bounds());
        cbox.expand(cents[indices[i]].center());
    }
    node.box = box;
    int n = hi - lo;

    if (n <= BVH_LEAF_SIZE) {
        node.left = -1;
        node.first = ordered.size();
        node.count = n;
        for (int i = lo; i < hi; i++) ordered.push(indices[i]);
        nodes.push(node);
        return node_idx;
    }
    // 最长轴
    int axis = cbox.longest_axis();
    // 按质心沿该轴插入排序（n 小，O(n^2) 可接受）
    for (int i = lo + 1; i < hi; i++) {
        int key = indices[i];
        rtfx kv = cents[key].center().x;
        if (axis == 1) kv = cents[key].center().y;
        else if (axis == 2) kv = cents[key].center().z;
        int j = i - 1;
        while (j >= lo) {
            rtfx jv = cents[indices[j]].center().x;
            if (axis == 1) jv = cents[indices[j]].center().y;
            else if (axis == 2) jv = cents[indices[j]].center().z;
            if (jv > kv) { indices[j + 1] = indices[j]; j--; }
            else break;
        }
        indices[j + 1] = key;
    }
    int mid = lo + n / 2;
    nodes.push(node);   // 占位，稍后填 children
    int lc = build_recurse(indices, lo, mid, cents);
    int rc = build_recurse(indices, mid, hi, cents);
    nodes[node_idx].left = lc;
    nodes[node_idx].count = 0;
    nodes[node_idx].first = rc;   // 复用 first 存右孩子下标
    return node_idx;
}

bool BVH::hit(const RTRay& ray, HitInfo& h) const {
    if (count <= 0) return false;
    // 显式栈
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;    // 根节点
    bool found = false;
    rtfx closest = ray.tmax;
    while (sp > 0) {
        int ni = stack[--sp];
        const BVHNode& node = nodes[ni];
        rtfx tn, tf;
        if (!rt_ray_aabb(ray, node.box, tn, tf)) continue;
        if (node.count > 0) {
            // 叶子：遍历图元
            for (int i = 0; i < node.count; i++) {
                int pi = ordered[node.first + i];
                // 注意：叶子索引是 build 时写入的 indices 区间，但我们
                // 直接存的是 lo..hi 区间内的 indices——这里简化为顺序图元
                // （场景构建时图元按 BVH 重排，或我们直接用 pi）
                rtfx t; RTVec3 n, pt; RTVec2 uv;
                if (prims[pi].intersect(ray, t, n, pt, uv)) {
                    if (t < closest && t > ray.tmin) {
                        closest = t;
                        h.t = t; h.normal = n; h.point = pt; h.uv = uv;
                        h.material = prims[pi].material;
                        h.prim_id = pi;
                        found = true;
                    }
                }
            }
        } else {
            // 内部：压入两个孩子（先远后近以利剪枝，这里简单压栈）
            stack[sp++] = node.left;
            stack[sp++] = node.first;   // 右孩子存在 first
        }
    }
    return found;
}

int bvh_self_test() {
    int fail = 0;
    // 一排球沿 X 轴排列，射线从 +X 打过来应命中最近的
    enum { N = 8 };
    Primitive* ps = new Primitive[N];
    for (int i = 0; i < N; i++) {
        ps[i] = Primitive::make_sphere(RTVec3(rt_itofx(i * 3), 0, 0), RT_ONE, 0);
    }
    BVH bvh;
    bvh.build(ps, N);
    // 射线从 x=30 朝 -X，应命中最右球 i=7（中心 x=21）
    RTRay ray(RTVec3(rt_itofx(30), 0, 0), RTVec3(-RT_ONE, 0, 0));
    HitInfo h;
    bool ok = bvh.hit(ray, h);
    if (!ok) fail++;
    if (h.prim_id != N - 1) fail++;   // 应命中最右球
    // 射线打向空中（y=10）应无命中
    RTRay miss(RTVec3(rt_itofx(30), rt_itofx(10), 0), RTVec3(-RT_ONE,0,0));
    HitInfo h2;
    if (bvh.hit(miss, h2)) fail++;
    delete[] ps;
    return fail;
}

} // namespace raytrace
} // namespace nefu
