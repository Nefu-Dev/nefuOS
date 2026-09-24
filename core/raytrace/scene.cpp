// ============================================================================
// nefuOS 光线追踪引擎 —— scene 实现（Q16.16 定点）
// ============================================================================
#include "scene.h"

namespace nefu {
namespace raytrace {

// 把 Q16.16 radiance 转 0xRRGGBB（Reinhard tonemap + gamma）
static uint32_t encode_color(const RTVec3& c) {
    rtfx rr = rt_div(c.x, RT_ONE + c.x);
    rtfx gg = rt_div(c.y, RT_ONE + c.y);
    rtfx bb = rt_div(c.z, RT_ONE + c.z);
    rr = rt_sqrt(rr); gg = rt_sqrt(gg); bb = rt_sqrt(bb);
    int R = rt_fxtoi(rr * rt_itofx(255));
    int G = rt_fxtoi(gg * rt_itofx(255));
    int B = rt_fxtoi(bb * rt_itofx(255));
    if (R < 0) R = 0; if (R > 255) R = 255;
    if (G < 0) G = 0; if (G > 255) G = 255;
    if (B < 0) B = 0; if (B > 255) B = 255;
    return ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

void Scene::load_preset(int which) {
    prims.erase_all(); mats.erase_all(); lights.erase_all();
    auto V=[](int a,int b,int cc){return RTVec3(rt_itofx(a),rt_itofx(b),rt_itofx(cc));};

    if (which == SCENE_CORNELL) {
        mats.push(Material::lambert(RTVec3(fx::fxf(9,10),fx::fxf(9,10),fx::fxf(9,10))));
        mats.push(Material::lambert(RTVec3(fx::fxf(8,10),fx::fxf(2,10),fx::fxf(2,10))));
        mats.push(Material::lambert(RTVec3(fx::fxf(2,10),fx::fxf(6,10),fx::fxf(2,10))));
        mats.push(Material::emissive(RTVec3(rt_itofx(4),rt_itofx(4),rt_itofx(4))));
        mats.push(Material::lambert(RTVec3(fx::fxf(8,10),fx::fxf(8,10),fx::fxf(8,10))));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));
        prims.push(Primitive::make_plane(V(0,-1,0), rt_itofx(4), 3));
        prims.push(Primitive::make_plane(V(1,0,0), -rt_itofx(3), 1));
        prims.push(Primitive::make_plane(V(-1,0,0), rt_itofx(3), 2));
        prims.push(Primitive::make_plane(V(0,0,1), -rt_itofx(3), 0));
        prims.push(Primitive::make_sphere(V(-1,1,-1), RT_ONE, 4));
        prims.push(Primitive::make_sphere(V(1,fx::fxf(7,10),1), fx::fxf(7,10), 4));
        lights.push(Light::area(V(0, rt_itofx(4), 0),
                                V(1,0,0)*fx::fxf(1,2), V(0,0,1)*fx::fxf(1,2),
                                RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(2)));
        cam.look_at(V(0,2,8), V(0,2,0), V(0,1,0));
        cam.fovy = fx::fxf(50,180)*RT_PI;
    } else if (which == SCENE_SPHERES) {
        mats.push(Material::lambert(RTVec3(fx::fxf(7,10),fx::fxf(7,10),fx::fxf(7,10))));
        mats.push(Material::lambert(RTVec3(fx::fxf(9,10),fx::fxf(5,10),fx::fxf(3,10))));
        mats.push(Material::metal(RTVec3(fx::fxf(9,10),fx::fxf(9,10),fx::fxf(9,10)), fx::fxf(1,10)));
        mats.push(Material::dielectric(fx::fxf(3,2)));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));
        prims.push(Primitive::make_sphere(V(-2,1,0), RT_ONE, 1));
        prims.push(Primitive::make_sphere(V(0,1,0), RT_ONE, 2));
        prims.push(Primitive::make_sphere(V(2,1,0), RT_ONE, 3));
        lights.push(Light::point(V(-2,5,2), RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(2)));
        lights.push(Light::ambient(RTVec3(fx::fxf(3,4),fx::fxf(3,4),fx::fxf(4,5)),
                                   RTVec3(fx::fxf(1,4),fx::fxf(1,4),fx::fxf(1,5)),
                                   fx::fxf(1,5)));
        cam.look_at(RTVec3(0, rt_itofx(1)+fx::fxf(1,2), rt_itofx(6)), V(0,1,0), V(0,1,0));
        cam.fovy = fx::fxf(60,180)*RT_PI;
    } else if (which == SCENE_GLASS) {
        mats.push(Material::lambert(RTVec3(fx::fxf(5,10),fx::fxf(5,10),fx::fxf(5,10))));
        mats.push(Material::dielectric(fx::fxf(3,2)));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));
        prims.push(Primitive::make_sphere(RTVec3(0, rt_itofx(1)+fx::fxf(1,2), 0), RT_ONE, 1));
        RTVec3 d = RTVec3(RT_ONE, -RT_ONE, fx::fxf(1,2)).normalized();
        lights.push(Light::directional(d, RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE));
        lights.push(Light::ambient(RTVec3(fx::fxf(4,5),fx::fxf(5,10),fx::fxf(9,10)),
                                   RTVec3(fx::fxf(1,5),fx::fxf(1,5),fx::fxf(2,5)),
                                   fx::fxf(2,5)));
        cam.look_at(V(0,2,5), V(0,1,0), V(0,1,0));
        cam.fovy = fx::fxf(55,180)*RT_PI;
    }
    else if (which == SCENE_ROOM) {
        // 简单房间：地面+四面墙+窗光
        mats.push(Material::lambert(RTVec3(fx::fxf(8,10),fx::fxf(8,10),fx::fxf(8,10))));
        mats.push(Material::emissive(RTVec3(rt_itofx(3),rt_itofx(3),rt_itofx(3))));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));            // 地面
        prims.push(Primitive::make_plane(V(0,-1,0), rt_itofx(4), 0)); // 顶
        prims.push(Primitive::make_plane(V(1,0,0), -rt_itofx(4), 0));
        prims.push(Primitive::make_plane(V(-1,0,0), rt_itofx(4), 0));
        prims.push(Primitive::make_plane(V(0,0,1), -rt_itofx(4), 0));
        // 窗光（面光在 +Z 墙）
        lights.push(Light::area(V(0, rt_itofx(2), rt_itofx(3)),
                                V(1,0,0)*fx::fxf(1,2), V(0,1,0)*fx::fxf(1,2),
                                RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(2)));
        lights.push(Light::ambient(RTVec3(fx::fxf(2,10),fx::fxf(2,10),fx::fxf(3,10)),
                                   RTVec3(fx::fxf(1,10),fx::fxf(1,10),fx::fxf(1,10)),
                                   fx::fxf(2,10)));
        cam.look_at(V(0,2,7), V(0,2,0), V(0,1,0));
        cam.fovy = fx::fxf(60,180)*RT_PI;
    } else if (which == SCENE_STAIRS) {
        // 阶梯：一排递增高度的盒子（用球近似占位）
        mats.push(Material::lambert(RTVec3(fx::fxf(7,10),fx::fxf(7,10),fx::fxf(8,10))));
        mats.push(Material::lambert(RTVec3(fx::fxf(8,10),fx::fxf(4,10),fx::fxf(2,10))));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));
        for (int i = 0; i < 6; i++) {
            prims.push(Primitive::make_sphere(V(i, i+1, 0), fx::fxf(1,2), (i&1)?1:0));
        }
        lights.push(Light::point(V(0, rt_itofx(6), rt_itofx(3)),
                                 RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(2)));
        lights.push(Light::ambient(RTVec3(fx::fxf(3,4),fx::fxf(3,4),fx::fxf(4,5)),
                                   RTVec3(fx::fxf(1,4),fx::fxf(1,4),fx::fxf(1,5)),
                                   fx::fxf(1,5)));
        cam.look_at(V(3,3,8), V(3,2,0), V(0,1,0));
        cam.fovy = fx::fxf(55,180)*RT_PI;
    }
    else if (which == SCENE_FOREST) {
        // 森林：地面 + 一排树（球冠）+ 太阳
        mats.push(Material::lambert(RTVec3(fx::fxf(4,10),fx::fxf(6,10),fx::fxf(3,10))));
        mats.push(Material::lambert(RTVec3(fx::fxf(2,10),fx::fxf(5,10),fx::fxf(2,10))));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 0));
        for (int i = -3; i <= 3; i++) {
            for (int j = -3; j <= 3; j++) {
                prims.push(Primitive::make_sphere(V(i*rt_itofx(2), rt_itofx(2), j*rt_itofx(2)),
                                                  rt_itofx(1), 1));
            }
        }
        lights.push(Light::directional(RTVec3(fx::fxf(1,2),1,fx::fxf(1,2)).normalized(),
                               RTVec3(RT_ONE,fx::fxf(9,10),fx::fxf(7,10)), rt_itofx(1)));
        lights.push(Light::ambient(RTVec3(fx::fxf(2,10),fx::fxf(2,10),fx::fxf(3,10)),
                                   RTVec3(fx::fxf(1,10),fx::fxf(1,10),fx::fxf(1,10)),
                                   fx::fxf(1,10)));
        cam.look_at(V(0,2,12), V(0,2,0), V(0,1,0));
        cam.fovy = fx::fxf(60,180)*RT_PI;
    }
    else if (which == SCENE_MIRROR) {
        // 镜面大厅：地面 + 墙 + 金属球
        mats.push(Material::lambert(RTVec3(RT_ONE,RT_ONE,RT_ONE)));
        mats.push(Material::lambert(RTVec3(fx::fxf(8,10),fx::fxf(8,10),fx::fxf(9,10))));
        prims.push(Primitive::make_plane(V(0,1,0), 0, 1));
        prims.push(Primitive::make_plane(V(0,0,1), -rt_itofx(6), 1));
        prims.push(Primitive::make_sphere(V(0, rt_itofx(2), 0), rt_itofx(2), 0));
        lights.push(Light::point(V(0, rt_itofx(5), rt_itofx(3)),
                                 RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(2)));
        lights.push(Light::ambient(RTVec3(fx::fxf(2,10),fx::fxf(2,10),fx::fxf(3,10)),
                                   RTVec3(fx::fxf(1,10),fx::fxf(1,10),fx::fxf(1,10)),
                                   fx::fxf(1,10)));
        cam.look_at(V(0,2,8), V(0,2,0), V(0,1,0));
        cam.fovy = fx::fxf(55,180)*RT_PI;
    }
    build();
}

void Scene::build() {
    if (prims.size() > 0)
        bvh.build(&prims[0], prims.size());
}

RTVec3 Scene::shade_pixel(rtfx u, rtfx v, RTRng& rng) const {
    RTRay ray = cam.generate_ray(u, v, rng);
    return tracer.trace(ray, 0, rng, bvh, &mats[0], mats.size(),
                        &lights[0], lights.size());
}

void Scene::render(uint32_t* fb, int w, int h, int samples,
                   void (*progress_cb)(int y, void* user), void* user) const {
    RTRng rng(0x12345678);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            RTVec3 acc(0, 0, 0);
            for (int s = 0; s < samples; s++) {
                rtfx u = rt_div(rt_itofx(x) + rng.next_fx(), rt_itofx(w));
                rtfx v = rt_div(rt_itofx(h - 1 - y) + rng.next_fx(), rt_itofx(h));
                acc = acc + shade_pixel(u, v, rng);
            }
            rtfx inv_s = rt_div(RT_ONE, rt_itofx(samples));
            fb[y * w + x] = encode_color(acc * inv_s);
        }
        if (progress_cb) progress_cb(y, user);
    }
}

int scene_self_test() {
    int fail = 0;
    for (int s = 0; s < SCENE_COUNT; s++) {
        Scene sc;
        sc.load_preset(s);
        if (sc.prims.size() <= 0) fail++;
        if (sc.mats.size() <= 0) fail++;
    }
    {
        Scene sc;
        sc.load_preset(SCENE_SPHERES);
        RTRng rng(42);
        RTVec3 c = sc.shade_pixel(fx::fxf(1,2), fx::fxf(1,2), rng);
        if (c.x <= 0 && c.y <= 0 && c.z <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu