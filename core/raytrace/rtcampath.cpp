// ============================================================================
// nefuOS 光线追踪引擎 —— rtcampath 实现（Q16.16 定点）
// ============================================================================
#include "rtcampath.h"

namespace nefu {
namespace raytrace {

RTVec3 RTCamPath::position() const {
    rtfx x = rt_mul(radius, fx::fx_cos(angle));
    rtfx z = rt_mul(radius, fx::fx_sin(angle));
    return RTVec3(target.x + x, target.y + height, target.z + z);
}

RTCamera& RTCamPath::step() {
    angle += speed;
    if (angle > RT_2PI) angle -= RT_2PI;
    RTVec3 eye = position();
    cam.look_at(eye, target, RTVec3(0, RT_ONE, 0));
    return cam;
}

int rtcampath_self_test() {
    int fail = 0;
    RTCamPath p;
    p.look_at(RTVec3(0,0,0), rt_itofx(5), rt_itofx(2));
    // 1. 起始位置在 radius 处
    {
        RTVec3 pos = p.position();
        rtfx d = (pos - RTVec3(0,2,0)).length();
        if (!rt_near(d, rt_itofx(5), rt_itofx(1))) fail++;
    }
    // 2. 推进若干帧后相机仍在 radius 处
    {
        for (int i = 0; i < 50; i++) p.step();
        RTVec3 pos = p.position();
        rtfx d = (pos - RTVec3(0,2,0)).length();
        if (!rt_near(d, rt_itofx(5), rt_itofx(1))) fail++;
    }
    // 3. 相机方向变化
    {
        RTVec3 p1 = p.position();
        for (int i = 0; i < 100; i++) p.step();
        RTVec3 p2 = p.position();
        if (rt_near(p1.x, p2.x, rt_itofx(1)) && rt_near(p1.z, p2.z, rt_itofx(1))) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
