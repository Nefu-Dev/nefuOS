// ============================================================================
// nefu::minilang —— 内置函数（builtin.h）
// ----------------------------------------------------------------------------
// 把 print / len / range / type / str / int / abs / max / min / sum / sort 等
// 内置 C 函数注册进全局环境。所有内置函数签名统一为 BuiltinFn，返回值自带
// 一次引用所有权；参数 argv 由调用方持有，内置函数用完即弃。
//
// print() 的输出不直接走 stdout，而是写入一个可替换的输出槽
// g_minilang_out（一个字符缓冲），方便宿主测试捕获、裸机转发到调试通道。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"
#include "env.h"

namespace nefu {
namespace minilang {

// 输出槽：内置 print 把文本追加到这里
extern char  g_minilang_out[];
extern int   g_minilang_out_len;
void  minilang_out_reset();
void  minilang_out_write(const char* s);

// 把全部内置函数注册进 env（通常是全局环境）
void builtin_register_all(Env* env);

// 自测试：返回失败数
int builtin_self_test();

} // namespace minilang
} // namespace nefu
