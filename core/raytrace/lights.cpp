// ============================================================================
// nefuOS 光线追踪引擎 —— lights 实现（Q16.16 定点）
// ============================================================================
#include "lights.h"

namespace nefu {
namespace raytrace {

bool Light::illuminate(const RTVec3& p, const RTVec3& n, RTRng& rng,
                       RTVec3& out_dir, rtfx& out_dist, RTVec3& radiance) const {
    out_dir = RTVec3(0, 0, 0);
    out_dist = RT_INF;
    radiance = RTVec3(0, 0, 0);
    switch (type) {
    case LIGHT_POINT: {
        RTVec3 to = pos - p;
        rtfx d = to.length();
        if (d < 1) return false;
        out_dir = to / d;
        out_dist = d;
        // 距离平方衰减
        rtfx atten = rt_div(RT_ONE, rt_mul(d, d));
        rtfx ndl = out_dir.dot(n);
        if (ndl < 0) ndl = 0;
        radiance = color * rt_mul(rt_mul(intensity, atten), ndl);
        return true;
    }
    case LIGHT_DIR: {
        out_dir = -dir;          // 从表面指向光源
        out_dist = RT_INF;
        rtfx ndl = out_dir.dot(n);
        if (ndl < 0) ndl = 0;
        radiance = color * rt_mul(intensity, ndl);
        return true;
    }
    case LIGHT_SPOT: {
        RTVec3 to = pos - p;
        rtfx d = to.length();
        if (d < 1) return false;
        out_dir = to / d;
        out_dist = d;
        // 聚光：从光源看表面的方向与光源朝向的夹角
        RTVec3 ldir = -out_dir;
        rtfx c = ldir.dot(dir);
        rtfx spot = RT_ONE;
        if (c < outer) spot = 0;
        else if (c < cutoff) {
            // 平滑过渡
            spot = rt_div(c - outer, cutoff - outer);
        }
        rtfx ndl = out_dir.dot(n);
        if (ndl < 0) ndl = 0;
        rtfx atten = rt_div(RT_ONE, rt_mul(d, d));
        radiance = color * rt_mul(rt_mul(rt_mul(intensity, atten), ndl), spot);
        return true;
    }
    case LIGHT_AREA: {
        // 在矩形面上均匀采样一点
        rtfx u = rng.next_fx();
        rtfx v = rng.next_fx();
        RTVec3 samp = pos + edge1 * u + edge2 * v;
        RTVec3 to = samp - p;
        rtfx d = to.length();
        if (d < 1) return false;
        out_dir = to / d;
        out_dist = d;
        rtfx ndl = out_dir.dot(n);
        if (ndl < 0) ndl = 0;
        radiance = color * rt_mul(intensity, ndl);
        return true;
    }
    case LIGHT_AMBIENT: {
        // 程序化天空：根据法线 y 分量在上下色间插值
        rtfx t = rt_clamp((n.y + RT_ONE) / rt_itofx(2), 0, RT_ONE);
        RTVec3 sky = rt_lerp(sky_bot, sky_top, t);
        radiance = sky * intensity;
        out_dir = RTVec3(0, 1, 0);
        out_dist = RT_INF;
        return true;
    }
    default:
        return false;
    }
}

rtfx Light::area_pdf() const {
    if (type != LIGHT_AREA) return 0;
    rtfx a = rt_mul(edge1.length(), edge2.length());
    return a > 0 ? rt_div(RT_ONE, a) : 0;
}

RTAABB Light::bounds() const {
    RTAABB b;
    switch (type) {
    case LIGHT_POINT: case LIGHT_SPOT:
        b.expand(pos); b.expand(pos + RTVec3(1,1,1)); break;
    case LIGHT_AREA:
        b.expand(pos - edge1 - edge2);
        b.expand(pos + edge1 + edge2); break;
    default: break;
    }
    return b;
}

int lights_self_test() {
    int fail = 0;
    RTRng rng(7);
    auto V=[](int a,int b,int cc){return RTVec3(rt_itofx(a),rt_itofx(b),rt_itofx(cc));};
    // 1. 点光：从上方照平面原点，法线朝上
    {
        Light l = Light::point(RTVec3(0, rt_itofx(5), 0),
                              RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE);
        RTVec3 dir; rtfx dist; RTVec3 rad;
        bool ok = l.illuminate(RTVec3(0,0,0), V(0,1,0), rng, dir, dist, rad);
        if (!ok) fail++;
        if (dir.y <= 0) fail++;         // 方向应朝上
        if (rad.x <= 0) fail++;         // 应有贡献
    }
    // 2. 方向光：从 +Z 照 -Z
    {
        Light l = Light::directional(V(0,0,-1), RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE);
        RTVec3 dir; rtfx dist; RTVec3 rad;
        bool ok = l.illuminate(RTVec3(0,0,0), V(0,0,1), rng, dir, dist, rad);
        if (!ok) fail++;
        if (dist != RT_INF) fail++;
    }
    // 3. 环境光：法线朝上应得到天空色
    {
        Light l = Light::ambient(RTVec3(0,0,RT_ONE), RTVec3(0,0,fx::fxf(1,2)), RT_ONE);
        RTVec3 dir; rtfx dist; RTVec3 rad;
        bool ok = l.illuminate(RTVec3(0,0,0), V(0,1,0), rng, dir, dist, rad);
        if (!ok) fail++;
        if (rad.z <= 0) fail++;
    }
    // 4. 面光采样：应返回一个有效方向
    {
        Light l = Light::area(RTVec3(0, rt_itofx(5), 0),
                              RTVec3(1,0,0)*fx::fxf(1,2), RTVec3(0,0,1)*fx::fxf(1,2),
                              RTVec3(RT_ONE,RT_ONE,RT_ONE), RT_ONE);
        RTVec3 dir; rtfx dist; RTVec3 rad;
        bool ok = l.illuminate(RTVec3(0,0,0), V(0,1,0), rng, dir, dist, rad);
        if (!ok) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
