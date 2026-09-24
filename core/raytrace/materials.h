// ============================================================================
// nefuOS 光线追踪引擎 —— materials: 表面材质模型
// ----------------------------------------------------------------------------
// 材质决定光线命中表面后如何散射（反射/折射/吸收/自发光）。
// 支持：
//   Lambertian 漫反射 / Metal 金属 / Dielectric 电介质(玻璃) /
//   Emissive 自发光 / Mirror 镜面 / Glossy 光泽 / Anisotropic 各向异性
// 所有计算 Q16.16 定点；scatter() 用 PRNG 做蒙特卡洛方向扰动。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

// 材质类型
enum MatType {
    MAT_LAMBERT = 0,
    MAT_METAL,
    MAT_DIELECTRIC,
    MAT_EMISSIVE,
    MAT_MIRROR,
    MAT_GLOSSY,
    MAT_ANISO,
    MAT_COUNT
};

struct Material {
    int     type;
    RTVec3  albedo;       // 基础颜色 / 反射率
    rtfx    roughness;    // 0=镜面 1=极粗糙
    rtfx    ior;          // 电介质折射率（玻璃≈1.5）
    RTVec3  emission;     // 自发光（RGB，单位 radiance）
    rtfx    kspec;        // 光泽材质镜面权重

    Material() : type(MAT_LAMBERT), roughness(0), ior(fx::fxf(3,2)) {}

    static Material lambert(const RTVec3& a) {
        Material m; m.type = MAT_LAMBERT; m.albedo = a; return m;
    }
    static Material metal(const RTVec3& a, rtfx rough) {
        Material m; m.type = MAT_METAL; m.albedo = a; m.roughness = rough; return m;
    }
    static Material dielectric(rtfx idx) {
        Material m; m.type = MAT_DIELECTRIC; m.ior = idx;
        m.albedo = RTVec3(RT_ONE, RT_ONE, RT_ONE); return m;
    }
    static Material emissive(const RTVec3& e) {
        Material m; m.type = MAT_EMISSIVE; m.emission = e; return m;
    }
    static Material mirror() {
        Material m; m.type = MAT_MIRROR; m.albedo = RTVec3(RT_ONE,RT_ONE,RT_ONE); return m;
    }
    static Material glossy(const RTVec3& a, rtfx spec, rtfx rough) {
        Material m; m.type = MAT_GLOSSY; m.albedo = a; m.kspec = spec;
        m.roughness = rough; return m;
    }
    static Material aniso(const RTVec3& a, rtfx rough) {
        Material m; m.type = MAT_ANISO; m.albedo = a; m.roughness = rough; return m;
    }

    // 是否为发光材质
    bool is_emissive() const { return type == MAT_EMISSIVE; }

    // 散射：给定入射射线与命中记录，计算出射射线 out 与衰减 atten。
    // 返回 false 表示光线被吸收（无出射）。rng 用于蒙特卡洛采样。
    bool scatter(const RTRay& in, const HitInfo& h, RTRay& out,
                 RTVec3& atten, RTRng& rng) const;

    // 自发光 radiance
    RTVec3 emitted() const { return emission; }

private:
    // 把方向扰动一个由 roughness 控制的随机偏角（返回单位向量）
    RTVec3 perturb(const RTVec3& dir, rtfx rough, RTRng& rng) const;
};

// self test
int materials_self_test();

} // namespace raytrace
} // namespace nefu
