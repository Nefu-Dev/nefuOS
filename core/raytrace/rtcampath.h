// ============================================================================
// nefuOS 光线追踪引擎 —— rtcampath: 相机动画路径
// ----------------------------------------------------------------------------
// 生成相机轨迹（用于 turntable 旋转扫描、飞行动画）：
//   - orbit: 绕目标点水平旋转
//   - dolly: 沿视线方向推拉
//   - pan:   左右平移
// 每帧返回一个 RTCamera。全部 Q16.16。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "camera.h"

namespace nefu {
namespace raytrace {

struct RTCamPath {
    RTVec3 target;
    rtfx   radius;
    rtfx   height;
    rtfx   angle;       // 当前角度（弧度）
    rtfx   speed;       // 每帧角速度
    RTCamera cam;

    RTCamPath() : radius(rt_itofx(5)), height(rt_itofx(2)), angle(0), speed(fx::fxf(2,100)) {}

    void look_at(const RTVec3& tgt, rtfx r, rtfx h) {
        target = tgt; radius = r; height = h; angle = 0;
    }

    // 推进一帧，更新 cam
    RTCamera& step();
    // 当前位置
    RTVec3 position() const;
};

int rtcampath_self_test();

} // namespace raytrace
} // namespace nefu
