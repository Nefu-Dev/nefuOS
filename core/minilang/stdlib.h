// ============================================================================
// nefu::minilang —— 标准库（stdlib.h）
// ----------------------------------------------------------------------------
// 在 builtin 之上提供更丰富的库函数：
//   字符串：split(s, sep) / join(arr, sep) / replace(s, old, new) /
//            find(s, sub) / substr(s, start, len) / upper/lower
//   数学：  sin / cos / sqrt / pow / floor / ceil / pi 常量
//   IO：    print 已在 builtin；这里提供 input 占位（裸机无 stdin，返回空串）
// 全部基于 nefu::fx 定点数学，不依赖 FPU。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"
#include "env.h"

namespace nefu {
namespace minilang {

// 注册标准库函数到环境
void stdlib_register_all(Env* env);

// 自测试
int stdlib_self_test();

} // namespace minilang
} // namespace nefu
