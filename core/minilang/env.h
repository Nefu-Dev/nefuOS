// ============================================================================
// nefu::minilang —— 运行环境 / 作用域链（env.h）
// ----------------------------------------------------------------------------
// 每个函数调用 / 块都有一个 Env，parent 指向外层环境（闭包捕获链）。
// Env 带引用计数：闭包持有捕获环境的强引用；求值过程中栈上临时环境由
// 调用方手动 release。变量用名字线性查找（迷你语言，作用域浅，无需哈希）。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"

namespace nefu {
namespace minilang {

// 一个变量绑定：名字 + 值（值自带引用所有权）
struct Binding {
    String name;
    Value  val;
};

struct Env {
    Env* parent;        // 外层环境（可空=全局）
    List<Binding> vars;
    int   ref;          // 引用计数
};

// 创建环境（ref=1）；parent 为外层环境，不被本环境持有所有权（由闭包持有）
Env* env_new(Env* parent);

// 引用计数
void env_retain(Env* e);
void env_release(Env* e);

// 在当前环境定义新变量（val 所有权转入，调用方不应再 drop）
void env_define(Env* e, const char* name, Value val);

// 沿链赋值：找到第一个匹配名字的绑定并更新（旧值 drop，val dup 存入）。
// 返回 false 表示未定义。
bool env_assign(Env* e, const char* name, Value val);

// 沿链查找；返回绑定指针（借用，不增引用），未找到返回 0。
Value* env_lookup(Env* e, const char* name);

// 在当前环境（不向父级）定义/查找，用于局部块作用域
Binding* env_lookup_local(Env* e, const char* name);

// 自测试
int env_self_test();

} // namespace minilang
} // namespace nefu
