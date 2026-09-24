// ============================================================================
// nefuOS 光线追踪引擎 —— lights: 光源模型
// ----------------------------------------------------------------------------
// 支持：
//   点光源 point / 方向光 directional / 聚光 spot /
//   矩形面光 area / 环境光 ambient(程序化 HDRI 天空渐变)
// 面光提供采样函数（蒙特卡洛 NEE 用），其它光源解析求方向。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

enum LightType {
    LIGHT_POINT = 0,
    LIGHT_DIR,
    LIGHT_SPOT,
    LIGHT_AREA,
    LIGHT_AMBIENT,
    LIGHT_COUNT
};

struct Light {
    int     type;
    RTVec3  color;       // RGB 颜色（radiance 量级）
    RTVec3  pos;          // point/spot/area 中心
    RTVec3  dir;          // dir/spot 方向（单位）
    rtfx    intensity;    // 强度
    rtfx    cutoff;       // spot 内半角（弧度，cos 值存这里）
    rtfx    outer;        // spot 外半角 cos
    RTVec3  edge1, edge2; // area 光两条边（决定矩形大小）
    RTVec3  sky_top, sky_bot; // ambient 天空上下色

    Light() : type(LIGHT_POINT), intensity(RT_ONE), cutoff(0), outer(0) {}

    static Light point(const RTVec3& p, const RTVec3& c, rtfx i) {
        Light l; l.type = LIGHT_POINT; l.pos = p; l.color = c; l.intensity = i; return l;
    }
    static Light directional(const RTVec3& d, const RTVec3& c, rtfx i) {
        Light l; l.type = LIGHT_DIR; l.dir = d.normalized(); l.color = c; l.intensity = i; return l;
    }
    static Light spot(const RTVec3& p, const RTVec3& d, const RTVec3& c,
                      rtfx inner_cos, rtfx outer_cos, rtfx i) {
        Light l; l.type = LIGHT_SPOT; l.pos = p; l.dir = d.normalized();
        l.color = c; l.cutoff = inner_cos; l.outer = outer_cos; l.intensity = i; return l;
    }
    static Light area(const RTVec3& center, const RTVec3& e1, const RTVec3& e2,
                      const RTVec3& c, rtfx i) {
        Light l; l.type = LIGHT_AREA; l.pos = center; l.edge1 = e1; l.edge2 = e2;
        l.color = c; l.intensity = i; return l;
    }
    static Light ambient(const RTVec3& top, const RTVec3& bot, rtfx i) {
        Light l; l.type = LIGHT_AMBIENT; l.sky_top = top; l.sky_bot = bot;
        l.intensity = i; return l;
    }

    // 在表面点 p（法线 n）处计算该光源贡献：
    //   out_dir   : 指向光源的单位方向（供阴影射线用）
    //   out_dist  : 到光源距离（方向光为无穷远=RT_INF）
    //   radiance  : 该光源到达 p 的辐射亮度（含衰减/角度）
    // 返回 false 表示该方向无贡献（聚光夹角外等）
    bool illuminate(const RTVec3& p, const RTVec3& n, RTRng& rng,
                    RTVec3& out_dir, rtfx& out_dist, RTVec3& radiance) const;

    // 面光表面积（用于 pdf）
    rtfx area_pdf() const;

    // 包围盒（场景剔除用）
    RTAABB bounds() const;
};

int lights_self_test();

} // namespace raytrace
} // namespace nefu
