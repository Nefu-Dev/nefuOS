// ============================================================================
// nefu::minilang —— 栈式虚拟机（vm.h）
// ----------------------------------------------------------------------------
// 执行 compiler 产出的 CodeChunk。VM 维护一个值栈与一组调用帧（每帧有自己的
// 代码指针与环境）。变量读写通过 Env 作用域链，因此闭包/递归自然成立。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"
#include "env.h"
#include "compiler.h"

namespace nefu {
namespace minilang {

// 一个调用帧
struct VmFrame {
    CodeChunk* chunk;
    int   ip;
    Env*  env;      // 本帧局部环境（VM 持有所有权）
};

struct Vm {
    List<Value> stack;      // 值栈
    List<VmFrame> frames;   // 调用栈
    Env*  global;           // 全局环境（借用）
    bool  error;
    String errmsg;

    Vm(Env* g);
    ~Vm();

    // 运行 chunk 的 main_entry；返回栈顶结果（裸引用），出错返回 null。
    Value run(CodeChunk* cc);
};

// 便捷入口：解析 + 编译 + 运行一段源码。
Value minilang_run_vm(const char* source, Env* global_env, String* errmsg_out);

// 自测试
int vm_self_test();

} // namespace minilang
} // namespace nefu
