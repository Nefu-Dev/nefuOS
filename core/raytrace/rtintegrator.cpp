// ============================================================================
// nefuOS 光线追踪引擎 —— rtintegrator 实现（Q16.16 定点）
// ============================================================================
#include "rtintegrator.h"

namespace nefu {
namespace raytrace {

RTVec3 integrator_background(const RTRay& ray, const IntegratorContext& ctx) {
    rtfx t = rt_clamp((ray.dir.y + RT_ONE) / rt_itofx(2), 0, RT_ONE);
    return rt_lerp(ctx.sky_bot, ctx.sky_top, t);
}

static bool shadowed(const RTVec3& p, const RTVec3& dir, rtfx dist, const BVH* bvh) {
    RTRay s(p, dir, RT_EPS, dist);
    HitInfo h;
    return bvh->hit(s, h);
}

RTVec3 integrate_direct(const RTRay& ray, RTRng& rng, const IntegratorContext& ctx) {
    HitInfo h;
    if (!ctx.bvh->hit(ray, h)) return integrator_background(ray, ctx);
    if (h.material < 0 || h.material >= ctx.nmats) return RTVec3(0,0,0);
    const Material& m = ctx.mats[h.material];
    if (m.is_emissive()) return m.emitted();
    RTVec3 radiance(0,0,0);
    for (int i = 0; i < ctx.nlights; i++) {
        RTVec3 ldir; rtfx ldist; RTVec3 lrad;
        if (!ctx.lights[i].illuminate(h.point, h.normal, rng, ldir, ldist, lrad)) continue;
        if (!shadowed(h.point, ldir, ldist, ctx.bvh)) radiance += lrad;
    }
    // 漫反射 albedo 衰减
    return radiance * m.albedo;
}

RTVec3 integrate_whitted(const RTRay& ray, int depth, RTRng& rng, const IntegratorContext& ctx) {
    if (depth > 5) return RTVec3(0,0,0);
    HitInfo h;
    if (!ctx.bvh->hit(ray, h)) return integrator_background(ray, ctx);
    if (h.material < 0 || h.material >= ctx.nmats) return RTVec3(0,0,0);
    const Material& m = ctx.mats[h.material];
    if (m.is_emissive()) return m.emitted();
    RTVec3 radiance(0,0,0);
    for (int i = 0; i < ctx.nlights; i++) {
        RTVec3 ldir; rtfx ldist; RTVec3 lrad;
        if (!ctx.lights[i].illuminate(h.point, h.normal, rng, ldir, ldist, lrad)) continue;
        if (!shadowed(h.point, ldir, ldist, ctx.bvh)) radiance += lrad;
    }
    RTRay out; RTVec3 atten;
    if (m.scatter(ray, h, out, atten, rng)) {
        radiance = radiance + integrate_whitted(out, depth+1, rng, ctx) * atten;
    }
    return radiance;
}

RTVec3 integrate_path(const RTRay& ray, int depth, RTRng& rng, const IntegratorContext& ctx) {
    if (depth > 6) return RTVec3(0,0,0);
    HitInfo h;
    if (!ctx.bvh->hit(ray, h)) return integrator_background(ray, ctx);
    if (h.material < 0 || h.material >= ctx.nmats) return RTVec3(0,0,0);
    const Material& m = ctx.mats[h.material];
    if (m.is_emissive()) return m.emitted();
    RTVec3 radiance(0,0,0);
    for (int i = 0; i < ctx.nlights; i++) {
        RTVec3 ldir; rtfx ldist; RTVec3 lrad;
        if (!ctx.lights[i].illuminate(h.point, h.normal, rng, ldir, ldist, lrad)) continue;
        if (!shadowed(h.point, ldir, ldist, ctx.bvh)) radiance += lrad;
    }
    RTRay out; RTVec3 atten;
    if (!m.scatter(ray, h, out, atten, rng)) return radiance;
    if (depth >= 3) {
        rtfx p = rt_clamp(atten.x, fx::fxf(1,4), RT_ONE);
        if (rng.next_fx() > p) return radiance;
        rtfx inv = rt_div(RT_ONE, p);
        return radiance + integrate_path(out, depth+1, rng, ctx) * rt_mul(atten.x, inv);
    }
    return radiance + integrate_path(out, depth+1, rng, ctx) * atten;
}

RTVec3 integrate_ao(const RTRay& ray, RTRng& rng, const IntegratorContext& ctx, rtfx max_dist) {
    HitInfo h;
    if (!ctx.bvh->hit(ray, h)) return integrator_background(ray, ctx);
    int N = 8;
    int occ = 0;
    for (int i = 0; i < N; i++) {
        RTVec3 d = rt_cosine_hemisphere_sample(h.normal, rng.next_fx(), rng.next_fx());
        RTRay s(h.point, d, RT_EPS, max_dist);
        HitInfo oh;
        if (ctx.bvh->hit(s, oh)) occ++;
    }
    rtfx vis = rt_div(rt_itofx(N - occ), rt_itofx(N));
    return RTVec3(vis, vis, vis);
}

int rtintegrator_self_test() {
    int fail = 0;
    Primitive prims[2];
    prims[0] = Primitive::make_plane(RTVec3(0,RT_ONE,0), 0, 0);
    prims[1] = Primitive::make_sphere(RTVec3(0,rt_itofx(1),0), RT_ONE, 1);
    Material mats[2];
    mats[0] = Material::lambert(RTVec3(fx::fxf(7,10),fx::fxf(7,10),fx::fxf(7,10)));
    mats[1] = Material::lambert(RTVec3(RT_ONE,fx::fxf(4,10),fx::fxf(4,10)));
    Light lights[1];
    lights[0] = Light::directional(RTVec3(0,-1,-1).normalized(), RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE);
    BVH bvh; bvh.build(prims, 2);
    IntegratorContext ctx;
    ctx.bvh = &bvh; ctx.mats = mats; ctx.nmats = 2;
    ctx.lights = lights; ctx.nlights = 1;
    ctx.sky_top = RTVec3(fx::fxf(3,4),fx::fxf(3,4),RT_ONE);
    ctx.sky_bot = RTVec3(fx::fxf(1,4),fx::fxf(1,4),fx::fxf(1,4));
    RTRng rng(7);
    // 1. direct 打地面应有光
    {
        RTRay ray(RTVec3(0,rt_itofx(5),0), RTVec3(0,-RT_ONE,0));
        RTVec3 c = integrate_direct(ray, rng, ctx);
        if (c.x < 0 || c.y < 0 || c.z < 0) fail++;
    }
    // 2. whitted 不爆
    {
        RTRay ray(RTVec3(0,rt_itofx(5),0), RTVec3(0,-RT_ONE,0));
        RTVec3 c = integrate_whitted(ray, 0, rng, ctx);
        if (c.x > rt_itofx(80)) fail++;
    }
    // 3. path 不爆
    {
        RTRay ray(RTVec3(0,rt_itofx(5),0), RTVec3(0,-RT_ONE,0));
        RTVec3 c = integrate_path(ray, 0, rng, ctx);
        if (c.x > rt_itofx(80)) fail++;
    }
    // 4. ao 输出在 [0,1]
    {
        RTRay ray(RTVec3(0,rt_itofx(5),0), RTVec3(0,-RT_ONE,0));
        RTVec3 c = integrate_ao(ray, rng, ctx, rt_itofx(5));
        if (c.x < 0 || c.x > RT_ONE + 100) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
