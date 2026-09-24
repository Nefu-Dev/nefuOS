// ============================================================================
// nefuOS 3D 图形库 —— 聚合头
//   #include "gfx3d/gfx3d_all.h"
// 暴露：math3d / mesh / raster / light / scene
// 并提供 gfx3d_self_test() 汇总全部子模块自检失败数。
// ============================================================================
#pragma once

#include "math3d.h"
#include "mesh.h"
#include "raster.h"
#include "light.h"
#include "scene.h"
#include "ppm.h"
#include "renderer.h"
#include "bezier.h"
#include "shadows.h"
#include "tonemap.h"
#include "texture.h"
#include "camera.h"

namespace nefu { namespace gfx3d {

// 运行所有 gfx3d 模块自检，返回失败总数（0 == 全部通过）
inline int gfx3d_self_test() {
    int f = 0;
    f += math3d_self_test();
    f += mesh_self_test();
    f += raster_self_test();
    f += light_self_test();
    f += scene_self_test();
    f += ppm_self_test();
    f += renderer_self_test();
    f += bezier_self_test();
    f += shadows_self_test();
    f += tonemap_self_test();
    f += texture_self_test();
    f += camera_self_test();
    return f;
}

}} // namespace
