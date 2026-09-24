// ============================================================================
// nefuOS 光线追踪引擎 —— rtraypool: 射线池批量管理
// ----------------------------------------------------------------------------
// 模拟 GPU 射线批处理：
//   - 预分配射线数组
//   - 逐批遍历求交
//   - 记录存活/终止射线
// 为后续并行化留接口。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTRayPool {
    RTRay* rays;
    RTVec3* throughput;
    int*    active;
    int     count;
    int     cap;
    RTRayPool() : rays(0), throughput(0), active(0), count(0), cap(0) {}
    ~RTRayPool() { delete[] rays; delete[] throughput; delete[] active; }

    void alloc(int n);
    void push(const RTRay& r, const RTVec3& th);
    void kill(int i);
    int  alive() const;
};

int rtraypool_self_test();

} // namespace raytrace
} // namespace nefu
