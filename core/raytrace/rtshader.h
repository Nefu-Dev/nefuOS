// ============================================================================
// nefuOS 光线追踪引擎 —— rtshader: 简化着色节点
// ----------------------------------------------------------------------------
// 把材质着色抽象成节点：
//   - 基础色 * 纹理
//   - 法线扰动
//   - 发光
// 固定管线，无动态分配。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "texture.h"

namespace nefu {
namespace raytrace {

struct RTShader {
    RTVec3 base_color;
    int    tex_idx;       // -1 无纹理
    rtfx   emission;
    rtfx   roughness;

    RTShader() : tex_idx(-1), emission(0), roughness(fx::fxf(5,10)) {}

    RTVec3 shade(const RTVec2& uv, const Texture* tex) const;
};

int rtshader_self_test();

} // namespace raytrace
} // namespace nefu
