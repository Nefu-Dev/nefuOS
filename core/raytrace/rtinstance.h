// ============================================================================
// nefuOS 光线追踪引擎 —— rtinstance: 图元实例化
// ----------------------------------------------------------------------------
// 把同一图元放在不同变换下复用：
//   - 局部->世界变换矩阵
//   - 逆变换（射线变换到局部空间求交）
//   - 法线变换（逆转置）
// 节省内存，适合重复物体（森林、砖墙）。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

struct RTInstance {
    Primitive  base;        // 局部空间图元
    RTMat4     local_to_world;
    RTMat4     world_to_local;
    int        material;

    RTInstance() : material(0) {
        local_to_world = RTMat4::identity();
        world_to_local = RTMat4::identity();
    }

    // 设置平移
    void set_translate(const RTVec3& t);
    // 设置均匀缩放
    void set_scale(rtfx s);
    // 求交（把射线变到局部空间）
    bool intersect(const RTRay& ray, rtfx& t, RTVec3& normal, RTVec3& point) const;
    // 世界包围盒
    RTAABB bounds() const;
};

int rtinstance_self_test();

} // namespace raytrace
} // namespace nefu
