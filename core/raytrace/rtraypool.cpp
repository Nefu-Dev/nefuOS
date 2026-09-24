// ============================================================================
// nefuOS 光线追踪引擎 —— rtraypool 实现
// ============================================================================
#include "rtraypool.h"

namespace nefu {
namespace raytrace {

void RTRayPool::alloc(int n) {
    delete[] rays; delete[] throughput; delete[] active;
    cap = n; count = 0;
    rays = new RTRay[n];
    throughput = new RTVec3[n];
    active = new int[n];
}

void RTRayPool::push(const RTRay& r, const RTVec3& th) {
    if (count >= cap) return;
    rays[count] = r;
    throughput[count] = th;
    active[count] = 1;
    count++;
}

void RTRayPool::kill(int i) {
    if (i >= 0 && i < count) active[i] = 0;
}

int RTRayPool::alive() const {
    int n = 0;
    for (int i = 0; i < count; i++) if (active[i]) n++;
    return n;
}

int rtraypool_self_test() {
    int fail = 0;
    RTRayPool pool;
    pool.alloc(8);
    pool.push(RTRay(RTVec3(0,0,0), RTVec3(0,0,-1)), RTVec3(RT_ONE,RT_ONE,RT_ONE));
    pool.push(RTRay(RTVec3(1,0,0), RTVec3(0,0,-1)), RTVec3(RT_ONE,0,0));
    // 1. 初始存活
    {
        if (pool.alive() != 2) fail++;
    }
    // 2. 杀一个
    {
        pool.kill(0);
        if (pool.alive() != 1) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
