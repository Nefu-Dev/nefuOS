// ============================================================================
// nefuOS 光线追踪引擎 —— rtmis: 多重重要性采样 (MIS)
// ----------------------------------------------------------------------------
// 合并两条采样策略（BRDF 采样 vs 光源采样）时的权重：
//   - power heuristic: w_s = p_s^beta / sum(p_i^beta)
//   - 平衡启发式：1/N
// 用于减少焦散/亮边的方差。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// 幂启发式（beta=2 推荐）
rtfx mis_power_heuristic(rtfx pdf_brdf, rtfx pdf_light);
// 平衡启发式（N 条策略）
rtfx mis_balance_heuristic(rtfx pdf, int n);
// 组合两条策略的辐射度
RTVec3 mis_combine(const RTVec3& brdf_li, rtfx pdf_brdf,
                   const RTVec3& light_li, rtfx pdf_light);

int rtmis_self_test();

} // namespace raytrace
} // namespace nefu
