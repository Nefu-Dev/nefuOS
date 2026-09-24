// ============================================================================
// nefuOS 光线追踪引擎 —— camera 实现（Q16.16 定点）
// ============================================================================
#include "camera.h"

namespace nefu {
namespace raytrace {

void RTCamera::basis(RTVec3& fwd, RTVec3& right, RTVec3& upv) const {
    fwd = (target - eye).normalized();
    right = fwd.cross(up).normalized();
    upv = right.cross(fwd);
}

RTRay RTCamera::generate_ray(rtfx u, rtfx v, RTRng& rng) const {
    RTVec3 fwd, right, upv;
    basis(fwd, right, upv);

    // 屏幕空间半高 = tan(fovy/2)，半宽 = 半高*aspect
    rtfx half_h = rt_div(fx::fx_tan(rt_div(fovy, rt_itofx(2))), RT_ONE);
    rtfx half_w = rt_mul(half_h, aspect);

    // ndc：[-1,1]
    rtfx sx = rt_mul(u, rt_itofx(2)) - RT_ONE;
    rtfx sy = rt_mul(v, rt_itofx(2)) - RT_ONE;

    RTVec3 origin;
    RTVec3 dir;
    if (ortho) {
        origin = eye + right * rt_mul(sx, half_w) + upv * rt_mul(sy, half_h);
        dir = fwd;
    } else {
        RTVec3 p = eye + right * rt_mul(sx, half_w) + upv * rt_mul(sy, half_h) + fwd;
        dir = (p - eye).normalized();
        origin = eye;
    }

    // 景深：在光圈圆内扰动 origin，并把 ray 重新瞄准焦平面
    if (aperture > 0) {
        rtfx ru = rt_mul(rng.next_signed(), aperture);
        rtfx rv = rt_mul(rng.next_signed(), aperture);
        RTVec3 offset = right * ru + upv * rv;
        origin = origin + offset;
        // 焦平面点：沿原方向走 focus_dist
        RTVec3 focus = eye + dir * focus_dist;
        dir = (focus - origin).normalized();
    }
    return RTRay(origin, dir, RT_EPS, RT_INF);
}

int camera_self_test() {
    int fail = 0;
    RTRng rng(99);
    // 1. 透视相机在 +Z 看原点，中心射线应朝 -Z
    {
        RTCamera cam;
        cam.look_at(RTVec3(0,0,rt_itofx(5)), RTVec3(0,0,0), RTVec3(0,1,0));
        RTRay r = cam.generate_ray(fx::fxf(1,2), fx::fxf(1,2), rng);
        // 中心方向应朝 -Z
        if (r.dir.z > 0) fail++;
        if (rt_abs(r.dir.x) > fx::fxf(1,10)) fail++;
    }
    // 2. 相机基向量正交
    {
        RTCamera cam;
        cam.look_at(RTVec3(0,0,rt_itofx(5)), RTVec3(0,0,0), RTVec3(0,1,0));
        RTVec3 f, r, u;
        cam.basis(f, r, u);
        if (rt_abs(f.dot(r)) > fx::fxf(1,10)) fail++;
        if (rt_abs(f.dot(u)) > fx::fxf(1,10)) fail++;
    }
    // 3. 射线原点应在 eye 附近
    {
        RTCamera cam;
        cam.look_at(RTVec3(1,2,rt_itofx(5)), RTVec3(0,0,0), RTVec3(0,1,0));
        RTRay ray = cam.generate_ray(0, 0, rng);
        if (!rt_near(ray.origin.x, rt_itofx(1), rt_itofx(1))) fail++;
        if (!rt_near(ray.origin.z, rt_itofx(5), rt_itofx(1))) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
