// ============================================================================
// nefuOS 光线追踪引擎 —— rtrender: 渲染编排器
// ----------------------------------------------------------------------------
// 把相机、场景、积分器、累积器串成完整渲染管线：
//   - 逐像素采样
//   - 渐进累积
//   - 进度回调
// ============================================================================
#pragma once
#include "rtmath.h"
#include "rtaccum.h"
#include "camera.h"
#include "scene.h"
#include "pathtracer.h"

namespace nefu {
namespace raytrace {

struct RTRenderer {
    RTAccum     accum;
    Pathtracer  pt;
    int         spp;         // 每像素采样数
    int         current_pass;

    RTRenderer() : spp(4), current_pass(0) {}

    void setup(int w, int h);
    // 渲染一帧（所有采样），返回进度 0..1
    rtfx render_pass(const Scene& scene);
    // 取最终像素颜色
    uint32_t pixel(int x, int y) const;
};

int rtrender_self_test();

} // namespace raytrace
} // namespace nefu
