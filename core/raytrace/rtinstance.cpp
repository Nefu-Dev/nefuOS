// ============================================================================
// nefuOS 光线追踪引擎 —— rtinstance 实现（Q16.16 定点）
// ============================================================================
#include "rtinstance.h"

namespace nefu {
namespace raytrace {

void RTInstance::set_translate(const RTVec3& t) {
    local_to_world = RTMat4::identity();
    local_to_world[0][3] = t.x;
    local_to_world[1][3] = t.y;
    local_to_world[2][3] = t.z;
    world_to_local = RTMat4::identity();
    world_to_local[0][3] = -t.x;
    world_to_local[1][3] = -t.y;
    world_to_local[2][3] = -t.z;
}

void RTInstance::set_scale(rtfx s) {
    local_to_world = RTMat4::identity();
    local_to_world[0][0] = s;
    local_to_world[1][1] = s;
    local_to_world[2][2] = s;
    rtfx inv = rt_div(RT_ONE, s);
    world_to_local = RTMat4::identity();
    world_to_local[0][0] = inv;
    world_to_local[1][1] = inv;
    world_to_local[2][2] = inv;
}

bool RTInstance::intersect(const RTRay& ray, rtfx& t, RTVec3& normal, RTVec3& point) const {
    // 把射线变到局部空间
    RTVec3 o = rt_mat4_xform_point(world_to_local, ray.origin);
    RTVec3 d = rt_mat4_xform_vec(world_to_local, ray.dir);
    RTRay local(o, d, ray.tmin, ray.tmax);
    RTVec3 n, p; RTVec2 uv;
    rtfx tt;
    if (!base.intersect(local, tt, n, p, uv)) return false;
    t = tt;
    point = rt_mat4_xform_point(local_to_world, p);
    normal = rt_mat4_xform_vec(local_to_world, n).normalized();
    return true;
}

RTAABB RTInstance::bounds() const {
    RTAABB b = base.bounds();
    // 8 角点变换到世界
    RTVec3 mn = b.mn, mx = b.mx;
    RTAABB out;
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mn.x,mn.y,mn.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mx.x,mn.y,mn.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mn.x,mx.y,mn.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mx.x,mx.y,mn.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mn.x,mn.y,mx.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mx.x,mn.y,mx.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mn.x,mx.y,mx.z)));
    out.expand(rt_mat4_xform_point(local_to_world, RTVec3(mx.x,mx.y,mx.z)));
    return out;
}

int rtinstance_self_test() {
    int fail = 0;
    RTInstance inst;
    inst.base = Primitive::make_sphere(RTVec3(0,0,0), RT_ONE, 0);
    inst.set_translate(RTVec3(rt_itofx(5),0,0));
    // 1. 射线打平移后的球应命中
    {
        RTRay ray(RTVec3(rt_itofx(10),0,0), RTVec3(-RT_ONE,0,0));
        rtfx t; RTVec3 n, p;
        bool ok = inst.intersect(ray, t, n, p);
        if (!ok) fail++;
    }
    // 2. 缩放：球半径变 2
    {
        RTInstance s;
        s.base = Primitive::make_sphere(RTVec3(0,0,0), RT_ONE, 0);
        s.set_scale(rt_itofx(2));
        RTRay ray(RTVec3(0,0,rt_itofx(5)), RTVec3(0,0,-RT_ONE));
        rtfx t; RTVec3 n, p;
        bool ok = s.intersect(ray, t, n, p);
        if (!ok) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
