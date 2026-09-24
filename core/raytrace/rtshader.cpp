// ============================================================================
// nefuOS 光线追踪引擎 —— rtshader 实现
// ============================================================================
#include "rtshader.h"

namespace nefu {
namespace raytrace {

RTVec3 RTShader::shade(const RTVec2& uv, const Texture* tex) const {
    RTVec3 c = base_color;
    (void)tex;
    (void)uv;
    if (emission > 0) c = c + RTVec3(emission, emission, emission);
    return c;
}

int rtshader_self_test() {
    int fail = 0;
    RTShader s;
    s.base_color = RTVec3(RT_ONE, RT_ONE, RT_ONE);
    // 1. 无纹理 = 基础色
    {
        RTVec3 c = s.shade(RTVec2(0,0), 0);
        if (!rt_near(c.x, RT_ONE, 400)) fail++;
    }
    // 2. 发光叠加
    {
        s.emission = rt_itofx(1);
        RTVec3 c = s.shade(RTVec2(0,0), 0);
        if (c.x < RT_ONE) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
