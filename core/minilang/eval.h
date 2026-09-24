// ============================================================================
// nefu::minilang —— 树遍历解释器（eval.h）
// ----------------------------------------------------------------------------
// 直接遍历 AST 求值。EvalCtx 持有全局环境指针与错误/返回信号。函数调用
// 创建新的栈帧环境（parent = 闭包捕获环境），实现闭包与递归。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"
#include "env.h"
#include "ast.h"

namespace nefu {
namespace minilang {

// 解释器运行时上下文
struct EvalCtx {
    Env*   global;          // 全局环境（借用）
    bool   error;           // 运行时错误
    String errmsg;
    // return 信号：执行 return 语句时置位并填 retval
    bool   returning;
    Value  retval;

    EvalCtx() : global(0), error(false), errmsg(), returning(false), retval(), keep_prog(0), last_expr() {}
    ProgramObj* keep_prog;  // 当前程序（闭包借此延长 AST 寿命）
    Value  last_expr;       // REPL
};

// 求一个表达式，返回值自带一次引用（调用方 drop）
Value eval_expr(EvalCtx* ctx, Node* e, Env* env);

// 执行一条语句；遇 return 置 ctx->returning
void  eval_stmt(EvalCtx* ctx, Node* s, Env* env);

// 执行整棵程序；在给定全局环境里跑顶层语句。返回最后一条表达式语句的值
// （或 null），自带一次引用。
Value eval_program(EvalCtx* ctx, ProgramObj* prog, Env* global);

// 便捷入口：解析 + 注册内置 + 运行一段源码。
//   source      源代码
//   global_env  已有全局环境（内置会注册进去）；若为 0 则内部新建并释放
// 返回运行结果值（裸引用）；出错返回 null 且写 errmsg_out。
Value minilang_run(const char* source, Env* global_env, String* errmsg_out);

// 自测试
int eval_self_test();

} // namespace minilang
} // namespace nefu


