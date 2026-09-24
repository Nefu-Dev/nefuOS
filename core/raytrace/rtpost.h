// ============================================================================
// nefuOS 光线追踪引擎 —— rtpost: 后处理链
// ----------------------------------------------------------------------------
// 把多个后处理滤镜串成管线：
//   - 按顺序应用
//   - 每个滤镜原地修改帧缓冲
// ============================================================================
#pragma once
#include "rtmath.h"
#include "rtfilter.h"

namespace nefu {
namespace raytrace {

struct RTPostChain {
    int w, h;
    uint32_t* fb;
    RTPostChain() : w(0), h(0), fb(0) {}
    ~RTPostChain() { delete[] fb; }

    void alloc(int width, int height);
    void apply_blur();
    void apply_sharpen(rtfx amt);
    void apply_dither();
    void apply_brightness(rtfx b);
};

int rtpost_self_test();

} // namespace raytrace
} // namespace nefu
