// ============================================================================
// nefuOS 光线追踪引擎 —— camera: 虚拟相机
// ----------------------------------------------------------------------------
// 支持透视 / 正交两种投影；lookAt 定位；
// 景深（aperture + focus_dist）；运动模糊（快门时间抖动）。
// generate_ray(u,v) 把归一化屏幕坐标 [0,1]^2 映射成世界空间射线。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTCamera {
    RTVec3 eye;
    RTVec3 target;
    RTVec3 up;
    rtfx   fovy;        // 垂直视场角（弧度）
    rtfx   aspect;      // 宽高比 w/h
    bool   ortho;       // true=正交
    rtfx   aperture;    // 景深光圈（0=无景深）
    rtfx   focus_dist;  // 对焦距离
    rtfx   shutter;     // 运动模糊快门时长（0=无）

    RTCamera() : fovy(fx::fxf(60, 180)*RT_PI), aspect(fx::fxf(4,3)),
                 ortho(false), aperture(0), focus_dist(rt_itofx(10)),
                 shutter(0) {
        eye = RTVec3(0,0,0); target = RTVec3(0,0,-1); up = RTVec3(0,1,0);
    }

    void look_at(const RTVec3& e, const RTVec3& t, const RTVec3& u) {
        eye = e; target = t; up = u;
    }

    // u,v in [0,1]（u 从左到右，v 从下到上）。rng 用于景深/运动模糊抖动。
    RTRay generate_ray(rtfx u, rtfx v, RTRng& rng) const;

    // 相机基向量（外部查询用）
    void basis(RTVec3& fwd, RTVec3& right, RTVec3& upv) const;
};

int camera_self_test();

} // namespace raytrace
} // namespace nefu
