// ============================================================================
// nefu::minilang —— 聚合头（minilang_all.h）
// ----------------------------------------------------------------------------
// 一次性包含迷你语言解释器的所有模块，并提供 minilang_self_test() 汇总。
// ============================================================================
#pragma once

#include "value.h"
#include "token.h"
#include "ast.h"
#include "env.h"
#include "parser.h"
#include "builtin.h"
#include "eval.h"
#include "stdlib.h"
#include "compiler.h"
#include "vm.h"

namespace nefu {
namespace minilang {

// 汇总所有模块自测试，返回总失败数（0 表示全部通过）
inline int minilang_self_test() {
    int f = 0;
    f += value_self_test();
    f += token_self_test();
    f += ast_self_test();
    f += env_self_test();
    f += parser_self_test();
    f += builtin_self_test();
    f += eval_self_test();
    f += stdlib_self_test();
    f += compiler_self_test();
    f += vm_self_test();
    return f;
}

} // namespace minilang
} // namespace nefu
