// ============================================================================
// nefuOS 光线追踪引擎 —— rtsampler: 采样模式
// ----------------------------------------------------------------------------
// 蒙特卡洛积分质量很大程度取决于采样器：
//   - Stratified（分层）：把像素格子切成 n×n 个格子各取一点，降方差
//   - Halton（低差异序列）：确定性拟随机序列，分布更均匀
//   - Jitter（抖动）：在规则网格内加随机偏移
// 所有采样都返回 [0,1) 的 Q16.16 坐标。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTSampler {
    RTRng  rng;
    int    spp;        // samples per pixel
    int    cur;        // 当前样本计数

    RTSampler(uint32_t seed = 0) : rng(seed), spp(1), cur(0) {}

    void reset(int samples);

    // 下一个分层 2D 采样点（u,v in [0,1)）
    void sample_stratified(rtfx& u, rtfx& v);

    // Halton 序列第 i 个点（基 2/3）
    void sample_halton(int i, rtfx& u, rtfx& v);

    // 圆盘采样（景深用）： concentric mapping
    void sample_disk(rtfx& x, rtfx& y);

    // 余弦加权半球采样（重要性采样漫反射）
    RTVec3 sample_cosine_hemisphere(const RTVec3& normal);
};

int rtsampler_self_test();

} // namespace raytrace
} // namespace nefu
