// ============================================================================
// nefu::compiler —— 迷你链接器（linker.h）
// ----------------------------------------------------------------------------
// 把多个 ObjectFile 合并成一个扁平的可加载段：
//   1. 顺序拼接各目标文件的代码段，记录段基址；
//   2. 建立全局符号表：定义者优先，重复定义报错，未定义符号记录为外部引用；
//   3. 应用重定位：把 call/jmp 的相对偏移按符号地址回填；
//   4. 输出最终字节块与符号/未定义符号清单。
// 本迷你链接器产出一个内存镜像（不写真实 ELF 头），用于教学与展示。
// ============================================================================
#pragma once

#include "assembler.h"

namespace nefu {
namespace compiler {

// ---- 最终可加载镜像 ----
struct Executable {
    uint8_t* text;
    int text_len;
    int entry;                  // 入口点偏移（main）
    List<const char*> undefined; // 未定义外部符号
    String log;                 // 链接日志
    ~Executable();
};

// ---- 链接器 ----
struct Linker {
    List<ObjectFile*> objs;
    List<const char*> sym_names;    // 已定义符号
    List<int> sym_addrs;            // 对应地址
    List<int> sym_objidx;

    void add(ObjectFile* o);
    Executable* link();              // 合并 + 重定位
    int resolve(const char* sym);    // 查询符号地址，未定义返回 -1
    void dump_symbols(String& out);  // 符号表文本转储
    void emit_map(String& out);      // 生成 .map 风格符号映射（地址 -> 符号）
};

// 链接器自测试：返回失败数
int linker_self_test();

} // namespace compiler
} // namespace nefu
