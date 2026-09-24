// nefuOS 几何数学库 gfxmath —— STL 风格聚合头
// 一次 include 全部几何模块。自测：gfx_all_self_test() 汇总各模块失败数。
// 教学版：全部用 class / do-while / switch / std 模板 / cmath 实现，中文注释。
#pragma once

#include "gfxmath/vec3.h"
#include "gfxmath/mat4.h"
#include "gfxmath/quat.h"
#include "gfxmath/ray.h"
#include "gfxmath/aabb.h"
#include "gfxmath/camera.h"
#include "gfxmath/mesh.h"
#include "gfxmath/proj.h"

namespace nefu {
namespace gfx {

// 汇总自测：返回失败总数，0 表示全部通过
inline int gfx_all_self_test() {
    return Vec3::self_test() + Mat4::self_test() +
           Quat::self_test() + Ray::self_test() +
           Plane::self_test() + AABB::self_test() +
           Sphere::self_test() + Camera::self_test() +
           Mesh::self_test() + Projector::self_test() +
           Renderer::self_test();
}

} // namespace gfx
} // namespace nefu
