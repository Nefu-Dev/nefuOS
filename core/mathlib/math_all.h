// nefuOS 数学库 mathlib —— STL 风格聚合头
// 一次 include 全部数学模块。自测：math_all_self_test() 汇总各模块失败数。
// 教学版：全部用 class / do-while / switch / std 模板 / cmath 实现，中文注释。
#pragma once

#include "mathlib/bigint.h"
#include "mathlib/rational.h"
#include "mathlib/complex.h"
#include "mathlib/matrix.h"
#include "mathlib/fft.h"
#include "mathlib/polynomial.h"
#include "mathlib/stat.h"
#include "mathlib/regression.h"
#include "mathlib/prime.h"
#include "mathlib/random.h"
#include "mathlib/vector2.h"
#include "mathlib/gcd.h"

namespace nefu {
namespace mathx {

// 汇总自测：返回失败总数，0 表示全部通过
inline int math_all_self_test() {
    return bigint_self_test() + rational_self_test() + complex_self_test() +
           matrix_self_test() + fft_self_test() + polynomial_self_test() +
           stat_self_test() + regression_self_test() + prime_self_test() +
           random_self_test() + vector2_self_test() + gcd_self_test();
}

} // namespace mathx
} // namespace nefu
