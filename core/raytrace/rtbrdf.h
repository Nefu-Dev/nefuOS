// ============================================================================
// nefuOS 光线追踪引擎 —— rtbrdf: 微表面 BRDF 模型
// ----------------------------------------------------------------------------
// 物理渲染用的 Cook-Torrance 微表面模型：
//   - D(h)：GGX/Trowbridge-Reitz 法线分布函数
//   - G(l,v)：Smith 几何遮蔽（Smith-GGX）
//   - F(h,v)：Fresnel（Schlick 近似）
//   - BRDF 求值与重要性采样方向生成
// 用于金属/光泽材质的更真实反射。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct GGX {
    rtfx roughness;   // [0,1]，0=镜面
    rtfx ior;        // 相对折射率（金属用 1）

    GGX(rtfx r = fx::fxf(2,10), rtfx i = 1) : roughness(r), ior(i) {}

    // 法线分布函数
    rtfx ndf(const RTVec3& n, const RTVec3& h) const;
    // Smith 几何项（单个方向）
    rtfx g1(const RTVec3& n, const RTVec3& v) const;
    // Smith 双向几何
    rtfx geometry(const RTVec3& n, const RTVec3& v, const RTVec3& l) const;
    // Schlick Fresnel
    RTVec3 fresnel(rtfx cos_theta, const RTVec3& f0) const;
    // 重要性采样：生成反射方向
    RTVec3 sample(const RTVec3& n, RTRng& rng) const;
    // pdf
    rtfx pdf(const RTVec3& n, const RTVec3& h) const;
};

int rtbrdf_self_test();

} // namespace raytrace
} // namespace nefu
