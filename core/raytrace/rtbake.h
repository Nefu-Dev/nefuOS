// ============================================================================
// nefuOS 光线追踪引擎 —— rtbake: 光照贴图烘焙
// ----------------------------------------------------------------------------
// 离线预计算表面光照，存成 2D 光照贴图：
//   - 在网格表面参数化采样点
//   - 逐点追踪半球环境光遮蔽
//   - 输出亮度数组（供运行时直接采样）
// 用于静态场景的快速渲染。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "rtenv.h"

namespace nefu {
namespace raytrace {

struct RTBake {
    int w, h;
    rtfx* luma;     // w*h 个亮度值
    RTVec3* color;  // w*h 个颜色

    RTBake() : w(0), h(0), luma(0), color(0) {}
    ~RTBake() { delete[] luma; delete[] color; }

    void alloc(int width, int height);
    void clear();
    // 用环境光烘焙整个贴图（每像素采样 env）
    void bake_constant(const RTVec3& c);
    // 用环境渐变烘焙
    void bake_gradient(const RTVec3& top, const RTVec3& bot);
    // 取某点颜色
    RTVec3 sample(int x, int y) const;
    // 平均亮度
    rtfx avg_luma() const;
};

int rtbake_self_test();

} // namespace raytrace
} // namespace nefu
