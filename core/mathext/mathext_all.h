// nefuOS 数学扩展库 —— 聚合头
// 一次性引入全部数学扩展模块，并提供统一的汇总自检入口。
#pragma once

#include "complex.h"
#include "matrix.h"
#include "poly.h"
#include "statistics.h"
#include "numtheory.h"
#include "geometry.h"

namespace nefu {
namespace mathext {

// 运行所有子模块自检，返回总失败条数（0 = 全部通过）。
// 子项：complex / matrix / poly / statistics / numtheory / geometry。
inline int mathext_self_test() {
    int f = 0;
    f += complex_self_test();
    f += matrix_self_test();
    f += poly_self_test();
    f += statistics_self_test();
    f += numtheory_self_test();
    f += geometry_self_test();
    return f;
}

} // namespace mathext
} // namespace nefu
