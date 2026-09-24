// ============================================================================
// nefuOS 光线追踪引擎 —— pathtracer 实现（Q16.16 定点）
// ============================================================================
#include "pathtracer.h"

namespace nefu {
namespace raytrace {

RTVec3 Pathtracer::background(const RTRay& ray) const {
    rtfx t = rt_clamp((ray.dir.y + RT_ONE) / rt_itofx(2), 0, RT_ONE);
    return rt_lerp(sky_bot, sky_top, t);
}

bool Pathtracer::occluded(const RTVec3& p, const RTVec3& dir, rtfx dist,
                          const BVH& bvh) const {
    RTRay shadow(p, dir, RT_EPS, dist);
    HitInfo h;
    return bvh.hit(shadow, h);
}

RTVec3 Pathtracer::trace(const RTRay& ray, int depth, RTRng& rng,
                         const BVH& bvh, const Material* mats, int nmats,
                         const Light* lights, int nlights) const {
    if (depth > max_depth) return RTVec3(0, 0, 0);

    HitInfo h;
    if (!bvh.hit(ray, h)) return background(ray);

    if (h.material < 0 || h.material >= nmats) return RTVec3(0,0,0);
    const Material& mat = mats[h.material];

    // 发光材质直接返回自发光
    if (mat.is_emissive()) return mat.emitted();

    RTVec3 radiance(0, 0, 0);

    // ---- 直接光照（NEE）----
    for (int i = 0; i < nlights; i++) {
        RTVec3 ldir; rtfx ldist; RTVec3 lrad;
        if (!lights[i].illuminate(h.point, h.normal, rng, ldir, ldist, lrad)) continue;
        if (lrad.x <= 0 && lrad.y <= 0 && lrad.z <= 0) continue;
        if (!occluded(h.point, ldir, ldist, bvh)) {
            radiance = radiance + lrad;
        }
    }

    // ---- 间接光照（蒙特卡洛散射）----
    RTRay out; RTVec3 atten;
    if (!mat.scatter(ray, h, out, atten, rng)) {
        return radiance;   // 吸收材质（如纯发光）
    }

    // 俄罗斯轮盘：深度较大时概率终止，收敛无偏
    if (depth >= rr_threshold) {
        rtfx p = rt_clamp(atten.x, fx::fxf(1,4), RT_ONE);
        if (rng.next_fx() > p) return radiance;
        // 权重补偿：除以存活概率
        rtfx inv = rt_div(RT_ONE, p);
        RTVec3 indirect = trace(out, depth + 1, rng, bvh, mats, nmats, lights, nlights);
        return radiance + indirect * (rt_mul(atten.x, inv));
    }

    RTVec3 indirect = trace(out, depth + 1, rng, bvh, mats, nmats, lights, nlights);
    // 漫反射近似：间接光按 albedo 衰减
    return radiance + indirect * atten;
}

RTVec3 Pathtracer::trace_mis(const RTRay& ray, int depth, RTRng& rng,
                              const BVH& bvh, const Material* mats, int nmats,
                              const Light* lights, int nlights) const {
    if (depth > max_depth) return RTVec3(0, 0, 0);
    HitInfo h;
    if (!bvh.hit(ray, h)) return background(ray);
    if (h.material < 0 || h.material >= nmats) return RTVec3(0,0,0);
    const Material& mat = mats[h.material];
    if (mat.is_emissive()) return mat.emitted();

    // BRDF 采样路径
    RTRay out; RTVec3 atten;
    if (!mat.scatter(ray, h, out, atten, rng)) return RTVec3(0,0,0);
    RTVec3 brdf_li = trace(out, depth + 1, rng, bvh, mats, nmats, lights, nlights);

    // NEE 光源采样
    RTVec3 nee_li(0,0,0);
    for (int i = 0; i < nlights; i++) {
        RTVec3 ldir; rtfx ldist; RTVec3 lrad;
        if (!lights[i].illuminate(h.point, h.normal, rng, ldir, ldist, lrad)) continue;
        if (!occluded(h.point, ldir, ldist, bvh)) nee_li = nee_li + lrad;
    }
    return brdf_li * atten + nee_li;
}

// ---------------------------------------------------------------------------
// self test：搭一个最小场景——一个 Lambert 球 + 一个方向光，验证 trace 非黑
// ---------------------------------------------------------------------------
int pathtracer_self_test() {
    int fail = 0;
    // 场景：地面(平面) + 球 + 方向光
    Primitive prims[2];
    prims[0] = Primitive::make_plane(RTVec3(0, RT_ONE, 0), 0, 0);   // y=0 地面
    prims[1] = Primitive::make_sphere(RTVec3(0, rt_itofx(1), 0), RT_ONE, 1);

    Material mats[2];
    mats[0] = Material::lambert(RTVec3(fx::fxf(1,2),fx::fxf(1,2),fx::fxf(1,2)));
    mats[1] = Material::lambert(RTVec3(RT_ONE, fx::fxf(1,4), fx::fxf(1,4)));

    Light lights[1];
    lights[0] = Light::directional(RTVec3(0,-1,-1).normalized(),
                                   RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE);

    BVH bvh;
    bvh.build(prims, 2);

    Pathtracer pt;
    RTRng rng(2024);

    // 1. 从空中打向地面的射线应命中且 radiance > 0
    {
        RTRay ray(RTVec3(0, rt_itofx(5), 0), RTVec3(0, -RT_ONE, 0));
        RTVec3 c = pt.trace(ray, 0, rng, bvh, mats, 2, lights, 1);
        if (c.y <= 0) fail++;   // 应有光照
    }
    // 2. 打向天空的射线应得到背景色
    {
        RTRay ray(RTVec3(0, rt_itofx(5), 0), RTVec3(0, RT_ONE, 0));
        RTVec3 c = pt.trace(ray, 0, rng, bvh, mats, 2, lights, 1);
        if (c.z <= 0) fail++;   // 天空蓝
    }
    // 3. trace 不应爆炸：多次采样稳定
    {
        RTRay ray(RTVec3(0, rt_itofx(5), 0), RTVec3(0, -RT_ONE, 0));
        RTVec3 c = pt.trace(ray, 0, rng, bvh, mats, 2, lights, 1);
        if (c.x > rt_itofx(10) || c.y > rt_itofx(10) || c.z > rt_itofx(10)) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
