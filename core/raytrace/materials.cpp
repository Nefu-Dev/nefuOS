// ============================================================================
// nefuOS 光线追踪引擎 —— materials 实现（Q16.16 定点）
// ============================================================================
#include "materials.h"

namespace nefu {
namespace raytrace {

// 在 dir 方向基础上叠加一个由 rough 控制的随机偏移（单位球内采样后归一）
RTVec3 Material::perturb(const RTVec3& dir, rtfx rough, RTRng& rng) const {
    if (rough <= 2) return dir;
    // 随机切向偏移
    rtfx jx = rt_mul(rng.next_signed(), rough);
    rtfx jy = rt_mul(rng.next_signed(), rough);
    rtfx jz = rt_mul(rng.next_signed(), rough);
    RTVec3 d = dir + RTVec3(jx, jy, jz);
    return d.normalized();
}

bool Material::scatter(const RTRay& in, const HitInfo& h, RTRay& out,
                       RTVec3& atten, RTRng& rng) const {
    RTVec3 N = h.normal;
    // 确保法线朝向入射射线一侧
    if (N.dot(in.dir) > 0) N = -N;

    switch (type) {
    case MAT_LAMBERT: {
        // 余弦权重半球采样
        RTVec3 d = rt_cosine_hemisphere_sample(N, rng.next_fx(), rng.next_fx());
        out = RTRay(h.point, d, RT_EPS, RT_INF);
        atten = albedo;
        return true;
    }
    case MAT_MIRROR: {
        RTVec3 r = rt_reflect_vec(in.dir, N);
        out = RTRay(h.point, r, RT_EPS, RT_INF);
        atten = albedo;
        return true;
    }
    case MAT_METAL: {
        RTVec3 r = rt_reflect_vec(in.dir, N);
        r = perturb(r, roughness, rng);
        out = RTRay(h.point, r, RT_EPS, RT_INF);
        atten = albedo;
        return true;
    }
    case MAT_DIELECTRIC: {
        // 斯涅尔折射 + Schlick 反射率
        rtfx cosi = -in.dir.dot(N);
        rtfx eta;
        RTVec3 nn = N;
        // 判断内外：若 cosi>0 则在外侧
        bool entering = (cosi > 0);
        // 空气(1.0)/玻璃(ior) 比值
        eta = entering ? rt_div(RT_ONE, ior) : ior;
        RTVec3 refr;
        bool reflected = !rt_refract_vec(in.dir, nn, eta, refr);
        // Schlick：R0 = ((1-eta)/(1+eta))^2
        rtfx r0 = rt_div(RT_ONE - eta, RT_ONE + eta);
        r0 = rt_mul(r0, r0);
        rtfx reflect_prob = rt_schlick(rt_abs(cosi), r0);
        RTVec3 r = rt_reflect_vec(in.dir, nn);
        if (reflected || rng.next_fx() < reflect_prob) {
            out = RTRay(h.point, r, RT_EPS, RT_INF);
        } else {
            out = RTRay(h.point, refr, RT_EPS, RT_INF);
        }
        atten = RTVec3(RT_ONE, RT_ONE, RT_ONE);
        return true;
    }
    case MAT_EMISSIVE: {
        // 不散射，直接发光
        atten = RTVec3(0, 0, 0);
        return false;
    }
    case MAT_GLOSSY: {
        // 镜面 + 漫反射混合
        RTVec3 r = rt_reflect_vec(in.dir, N);
        r = perturb(r, roughness, rng);
        RTVec3 diff = rt_cosine_hemisphere_sample(N, rng.next_fx(), rng.next_fx());
        rtfx p = kspec;
        RTVec3 d = (rng.next_fx() < p) ? r : diff;
        out = RTRay(h.point, d, RT_EPS, RT_INF);
        atten = albedo;
        return true;
    }
    case MAT_ANISO: {
        // 各向异性：沿切线方向扰动更强（近似：用两个不同粗糙度）
        RTVec3 r = rt_reflect_vec(in.dir, N);
        RTVec3 up = (rt_abs(N.y) < RT_HALF) ? RTVec3(0, RT_ONE, 0) : RTVec3(RT_ONE, 0, 0);
        RTVec3 T = N.cross(up).normalized();
        rtfx jt = rt_mul(rng.next_signed(), roughness);
        rtfx jb = rt_mul(rng.next_signed(), rt_div(roughness, rt_itofx(4)));
        r = (r + T * jt + N * jb).normalized();
        out = RTRay(h.point, r, RT_EPS, RT_INF);
        atten = albedo;
        return true;
    }
    default:
        atten = RTVec3(0, 0, 0);
        return false;
    }
}

// ---------------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------------
int materials_self_test() {
    int fail = 0;
    RTRng rng(1234);
    auto V=[](int a,int b,int cc){return RTVec3(rt_itofx(a),rt_itofx(b),rt_itofx(cc));};

    // 1. Lambert：命中后必有出射方向，衰减=albedo
    {
        Material m = Material::lambert(RTVec3(fx::fxf(1,2), fx::fxf(1,2), fx::fxf(1,2)));
        HitInfo h; h.t = 1; h.normal = RTVec3(0, RT_ONE, 0); h.point = RTVec3(0,0,0);
        RTRay in(V(0,5,0), RTVec3(0, -RT_ONE, 0));
        RTRay out; RTVec3 atten;
        bool s = m.scatter(in, h, out, atten, rng);
        if (!s) fail++;
        if (out.dir.length() < RT_HALF) fail++;   // 出射方向应近似单位长
    }
    // 2. Mirror：方向应严格反射
    {
        Material m = Material::mirror();
        HitInfo h; h.t = 1; h.normal = RTVec3(0, RT_ONE, 0); h.point = RTVec3(0,0,0);
        RTRay in(V(1,5,0), V(-1,-1,0).normalized());
        RTRay out; RTVec3 atten;
        bool s = m.scatter(in, h, out, atten, rng);
        if (!s) fail++;
        // 入射 y 分量为负，反射后 y 分量应翻正
        if (out.dir.y <= 0) fail++;
    }
    // 3. Dielectric：垂直入射应产生折射
    {
        Material m = Material::dielectric(fx::fxf(3,2));
        HitInfo h; h.t = 1; h.normal = RTVec3(0, RT_ONE, 0); h.point = RTVec3(0,0,0);
        RTRay in(V(0,5,0), RTVec3(0, -RT_ONE, 0));
        RTRay out; RTVec3 atten;
        bool s = m.scatter(in, h, out, atten, rng);
        if (!s) fail++;
    }
    // 4. Emissive：不散射
    {
        Material m = Material::emissive(RTVec3(RT_ONE, RT_ONE, RT_ONE));
        HitInfo h; h.t = 1; h.normal = RTVec3(0,1,0);
        RTRay in(V(0,5,0), V(0,-1,0));
        RTRay out; RTVec3 atten;
        bool s = m.scatter(in, h, out, atten, rng);
        if (s) fail++;
        if (!rt_near(m.emitted().x, RT_ONE, 100)) fail++;
    }
    // 5. Metal：粗糙金属出射方向应与反射方向接近但有扰动
    {
        Material m = Material::metal(RTVec3(RT_ONE, RT_ONE, RT_ONE), fx::fxf(1,10));
        HitInfo h; h.t = 1; h.normal = RTVec3(0, RT_ONE, 0); h.point = RTVec3(0,0,0);
        RTRay in(V(0,5,0), RTVec3(0, -RT_ONE, 0));
        RTRay out; RTVec3 atten;
        bool s = m.scatter(in, h, out, atten, rng);
        if (!s) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
