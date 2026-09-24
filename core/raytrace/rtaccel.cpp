// ============================================================================
// nefuOS 光线追踪引擎 —— rtaccel 实现（Q16.16 定点，16 桶 SAH）
// ============================================================================
#include "rtaccel.h"

namespace nefu {
namespace raytrace {

static const int ACCEL_LEAF = 4;
static const int BUCKETS = 16;

void RTAccel::build(const Primitive* prim_array, int n) {
    nodes.erase_all();
    ordered.erase_all();
    prims = prim_array;
    count = n;
    if (n <= 0) return;
    int* idx = new int[n];
    RTAABB* boxes = new RTAABB[n];
    for (int i = 0; i < n; i++) {
        idx[i] = i;
        boxes[i] = prims[i].bounds();
    }
    build_recurse(idx, 0, n, boxes);
    delete[] idx;
    delete[] boxes;
}

int RTAccel::build_recurse(int* idx, int lo, int hi, const RTAABB* boxes) {
    int n = hi - lo;
    AccelNode node;
    RTAABB box;
    RTAABB cbox;
    for (int i = lo; i < hi; i++) {
        box.expand(boxes[idx[i]]);
        cbox.expand(boxes[idx[i]].center());
    }
    node.box = box;

    if (n <= ACCEL_LEAF) {
        node.left = -1;
        node.first = ordered.size();
        node.count = n;
        for (int i = lo; i < hi; i++) ordered.push(idx[i]);
        nodes.push(node);
        return nodes.size() - 1;
    }

    // 最长轴
    int axis = cbox.longest_axis();
    // 桶质心范围
    {
        rtfx cmin = cbox.mn.x, cmax = cbox.mx.x;
        if (axis == 1) { cmin = cbox.mn.y; cmax = cbox.mx.y; }
        else if (axis == 2) { cmin = cbox.mn.z; cmax = cbox.mx.z; }
        rtfx range = cmax - cmin;
        if (range <= 0) {
            // 退化：直接中位数切
            int mid = lo + n / 2;
            int ni = nodes.size();
            nodes.push(node);
            int lc = build_recurse(idx, lo, mid, boxes);
            int rc = build_recurse(idx, mid, hi, boxes);
            nodes[ni].left = lc;
            nodes[ni].first = rc;
            nodes[ni].count = 0;
            return ni;
        }
        // 桶计数与表面积
        int bcount[BUCKETS];
        RTAABB bbox[BUCKETS];
        for (int b = 0; b < BUCKETS; b++) bcount[b] = 0;
        for (int i = lo; i < hi; i++) {
            rtfx c = boxes[idx[i]].center().x;
            if (axis == 1) c = boxes[idx[i]].center().y;
            else if (axis == 2) c = boxes[idx[i]].center().z;
            int b = rt_fxtoi(rt_mul(rt_div(c - cmin, range), rt_itofx(BUCKETS)));
            if (b >= BUCKETS) b = BUCKETS - 1;
            if (b < 0) b = 0;
            bcount[b]++;
            bbox[b].expand(boxes[idx[i]]);
        }
        // 逐分裂面计算代价
        rtfx best_cost = RT_INF;
        int best_split = 0;
        for (int b = 0; b < BUCKETS - 1; b++) {
            RTAABB L, R;
            int nl = 0, nr = 0;
            for (int i = 0; i <= b; i++) { L.expand(bbox[i]); nl += bcount[i]; }
            for (int i = b + 1; i < BUCKETS; i++) { R.expand(bbox[i]); nr += bcount[i]; }
            if (nl == 0 || nr == 0) continue;
            rtfx cost = rt_mul(rt_itofx(nl), L.surface_area())
                      + rt_mul(rt_itofx(nr), R.surface_area());
            if (cost < best_cost) { best_cost = cost; best_split = b; }
        }
        // 按桶分裂重排 idx
        int mid = lo;
        for (int i = lo; i < hi; i++) {
            rtfx c = boxes[idx[i]].center().x;
            if (axis == 1) c = boxes[idx[i]].center().y;
            else if (axis == 2) c = boxes[idx[i]].center().z;
            int b = rt_fxtoi(rt_mul(rt_div(c - cmin, range), rt_itofx(BUCKETS)));
            if (b >= BUCKETS) b = BUCKETS - 1;
            if (b < 0) b = 0;
            if (b <= best_split) {
                int t = idx[mid]; idx[mid] = idx[i]; idx[i] = t;
                mid++;
            }
        }
        if (mid == lo || mid == hi) mid = lo + n / 2;
        int ni = nodes.size();
        nodes.push(node);
        int lc = build_recurse(idx, lo, mid, boxes);
        int rc = build_recurse(idx, mid, hi, boxes);
        nodes[ni].left = lc;
        nodes[ni].first = rc;
        nodes[ni].count = 0;
        return ni;
    }
}

bool RTAccel::hit(const RTRay& ray, HitInfo& h) const {
    if (count <= 0) return false;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    bool found = false;
    rtfx closest = ray.tmax;
    while (sp > 0) {
        int ni = stack[--sp];
        const AccelNode& node = nodes[ni];
        rtfx tn, tf;
        if (!rt_ray_aabb(ray, node.box, tn, tf)) continue;
        if (node.count > 0) {
            for (int i = 0; i < node.count; i++) {
                int pi = ordered[node.first + i];
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
            stack[sp++] = node.left;
            stack[sp++] = node.first;
        }
    }
    return found;
}

int rtaccel_self_test() {
    int fail = 0;
    enum { N = 16 };
    Primitive ps[N];
    for (int i = 0; i < N; i++)
        ps[i] = Primitive::make_sphere(RTVec3(rt_itofx(i * 2), 0, 0), RT_ONE, 0);
    RTAccel acc;
    acc.build(ps, N);
    if (acc.node_count() <= 0) fail++;
    // 射线从 +X 打向 -X，应命中最右球
    RTRay ray(RTVec3(rt_itofx(40), 0, 0), RTVec3(-RT_ONE, 0, 0));
    HitInfo h;
    bool ok = acc.hit(ray, h);
    if (!ok) fail++;
    if (h.prim_id != N - 1) fail++;
    // miss
    RTRay miss(RTVec3(rt_itofx(40), rt_itofx(10), 0), RTVec3(-RT_ONE,0,0));
    HitInfo h2;
    if (acc.hit(miss, h2)) fail++;
    return fail;
}

} // namespace raytrace
} // namespace nefu
