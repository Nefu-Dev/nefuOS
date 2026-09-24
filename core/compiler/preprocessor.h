// ============================================================================
// nefu::compiler —— C 预处理器（preprocessor.h）
// ----------------------------------------------------------------------------
// 支持子集：
//   * #define NAME value            对象式宏
//   * #define NAME(p1,p2) body      带参宏
//   * #undef NAME
//   * #ifdef / #ifndef / #else / #endif  条件编译
//   * #include "..."                模拟：记录包含路径，不真正读文件
//   * 宏展开（文本替换；带参宏做参数替换）
// 输出预处理后的源码字符串。
// ============================================================================
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace compiler {

struct Macro {
    String name;
    List<String> params;     // 带参宏参数列表（对象式宏为空）
    String body;
    bool is_func;
};

struct Preprocessor {
    List<Macro*> macros;
    List<String> includes;   // 记录 #include 路径
    String out;              // 预处理后输出
    int cond_depth;          // 条件栈深度
    List<int> cond_active;   // 每层是否处于活动分支
    List<int> cond_taken;    // 每层是否已有分支被采纳（#elif/#else 用）
    List<String> errors;     // #error 收集的错误信息
    int err_count;

    Preprocessor();
    ~Preprocessor();

    Macro* find(const char* name);
    void define_object(const char* name, const char* body);
    void define_func(const char* name, const char* params_csv, const char* body);
    void undef(const char* name);
    int macro_count() { return macros.size(); }   // 当前定义的宏数量
    String expand(const char* line);          // 对一行做宏展开
    const char* process(const char* source); // 整段预处理，返回 out.c_str()
};

// 预处理器自测试：返回失败数
int preprocessor_self_test();

} // namespace compiler
} // namespace nefu
