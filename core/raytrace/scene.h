// ============================================================================
// nefuOS 光线追踪引擎 —— scene: 场景描述与预设
// ----------------------------------------------------------------------------
// 聚合图元/材质/光源，构建 BVH，提供预设场景：
//   CORNELL  康奈尔盒（经典全局光照测试）
//   SPHERES  球体测试组（漫反射/金属/玻璃三球）
//   GLASS    玻璃球折射测试
// 并提供 render() 把相机 + 路径追踪结果写入帧缓冲。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"
#include "materials.h"
#include "lights.h"
#include "camera.h"
#include "bvh.h"
#include "pathtracer.h"
#include "../klib/klib.h"

namespace nefu {
namespace raytrace {

enum PresetScene {
    SCENE_CORNELL = 0,
    SCENE_SPHERES,
    SCENE_GLASS,
    SCENE_ROOM,
    SCENE_STAIRS,
    SCENE_FOREST,
    SCENE_MIRROR,
    SCENE_COUNT
};

struct Scene {
    nefu::List<Primitive> prims;
    nefu::List<Material>  mats;
    nefu::List<Light>     lights;
    BVH       bvh;
    RTCamera  cam;
    Pathtracer tracer;

    Scene() {}
    ~Scene() {}

    void load_preset(int which);
    void build();     // 重建 BVH

    // 渲染整个帧缓冲：fb 为 w*h 个 0xRRGGBB 像素
    // progress_cb 可空；samples 为每像素采样数
    void render(uint32_t* fb, int w, int h, int samples,
                void (*progress_cb)(int y, void* user), void* user) const;

    // 单像素着色（供预览/交互）
    RTVec3 shade_pixel(rtfx u, rtfx v, RTRng& rng) const;
};

int scene_self_test();

} // namespace raytrace
} // namespace nefu
