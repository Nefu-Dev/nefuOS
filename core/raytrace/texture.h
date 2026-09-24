// ============================================================================
// nefuOS 光线追踪引擎 —— texture: 程序化与图像纹理
// ----------------------------------------------------------------------------
// 支持：
//   棋盘格 checker / 线性渐变 gradient / 值噪声 noise /
//   程序化图像 image(条纹) / 法线贴图 normal_map / 凹凸贴图 bump_map
// 纹理根据 uv（或命中点世界坐标）返回颜色；法线/凹凸贴图返回扰动后的法线。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

enum TexType {
    TEX_CHECKER = 0,
    TEX_GRADIENT,
    TEX_NOISE,
    TEX_IMAGE,
    TEX_NORMAL,
    TEX_BUMP,
    TEX_SOLID,
    TEX_FBM,
    TEX_MARBLE,
    TEX_WOOD,
    TEX_COUNT
};

struct Texture {
    int     type;
    RTVec3  c1, c2;       // 两种颜色
    rtfx    scale;        // 棋盘/噪声缩放
    rtfx    bump_strength;// 凹凸强度

    Texture() : type(TEX_SOLID), scale(RT_ONE), bump_strength(fx::fxf(1,4)) {}

    static Texture solid(const RTVec3& c) {
        Texture t; t.type = TEX_SOLID; t.c1 = c; return t;
    }
    static Texture checker(const RTVec3& a, const RTVec3& b, rtfx s) {
        Texture t; t.type = TEX_CHECKER; t.c1 = a; t.c2 = b; t.scale = s; return t;
    }
    static Texture gradient(const RTVec3& a, const RTVec3& b) {
        Texture t; t.type = TEX_GRADIENT; t.c1 = a; t.c2 = b; return t;
    }
    static Texture noise(const RTVec3& a, const RTVec3& b, rtfx s) {
        Texture t; t.type = TEX_NOISE; t.c1 = a; t.c2 = b; t.scale = s; return t;
    }
    static Texture fbm(const RTVec3& a, const RTVec3& b, rtfx s) { Texture t; t.type=TEX_FBM; t.c1=a; t.c2=b; t.scale=s; return t; }
    static Texture marble(const RTVec3& a, const RTVec3& b, rtfx s) { Texture t; t.type=TEX_MARBLE; t.c1=a; t.c2=b; t.scale=s; return t; }
    static Texture wood(const RTVec3& a, const RTVec3& b, rtfx s) { Texture t; t.type=TEX_WOOD; t.c1=a; t.c2=b; t.scale=s; return t; }
    static Texture bump(rtfx strength) {
        Texture t; t.type = TEX_BUMP; t.bump_strength = strength; return t;
    }

    // 采样颜色：uv in [0,1]
    RTVec3 sample(const RTVec2& uv) const;
    // 用世界坐标采样（棋盘格常用）
    RTVec3 sample_world(const RTVec3& p) const;
    // 凹凸扰动法线：返回扰动后的法线
    RTVec3 perturb_normal(const RTVec3& n, const RTVec3& p) const;
};

int texture_self_test();

} // namespace raytrace
} // namespace nefu
