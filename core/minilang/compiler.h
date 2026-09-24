// ============================================================================
// nefu::minilang —— AST 到字节码编译器（compiler.h）
// ----------------------------------------------------------------------------
// 把 AST 编译成栈式虚拟机可执行的 CodeChunk。字节码是紧凑的小端指令流：
//   每条指令 1 字节 opcode，后接 0~n 字节操作数（用 int32 编码）。
// 设计上与树遍历解释器平行：同一棵 AST 既能 eval 又能 compile+vm 运行。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"
#include "ast.h"

namespace nefu {
namespace minilang {

// 操作码
enum OpCode {
    BC_CONST = 0,     // push consts[idx]
    BC_TRUE, BC_FALSE, BC_NULL,
    BC_ADD, BC_SUB, BC_MUL, BC_DIV, BC_MOD,
    BC_EQ, BC_NEQ, BC_LT, BC_GT, BC_LE, BC_GE,
    BC_NOT, BC_NEG,
    BC_LOAD_NAME,     // push 名字对应的值（从当前环境查找）
    BC_DEF_NAME,      // pop 值，在当前环境 define
    BC_STORE_NAME,    // pop 值，沿链 assign
    BC_JMP,           // 无条件跳 int32
    BC_JZ,            // pop；若假则跳 int32
    BC_CALL,          // 调用 int argc
    BC_RET,           // 返回（弹出返回地址/栈帧由 VM 管理）
    BC_POP,           // 丢弃栈顶
    BC_ARRAY,         // 弹出 n 个，构造数组压入
    BC_INDEX,         // 弹出 idx、obj，压入 obj[idx]
    BC_SET_INDEX,     // 弹出 idx、obj、val，写回
    BC_CLOSURE        // 压入函数闭包（函数原型 idx）
};

// 函数原型：在一个 chunk 里记录每个函数的入口偏移与形参个数
struct FuncProto {
    int entry;
    int nargs;
    const char* name;
    FuncNode* fn;       // 指向 AST（arena 拥有）
};

// 一个编译单元：指令流 + 常量池 + 名字池 + 函数表
struct CodeChunk {
    List<uint8_t> code;
    List<Value>   consts;     // 字面量常量（自带引用所有权）
    List<const char*> names;  // 标识符名（arena 拥有）
    List<FuncProto> funcs;
    int   main_entry;         // 顶层入口偏移
};

// 编译器：把 program 编译进 chunk
struct Compiler {
    ProgramObj* prog;
    bool  error;
    String errmsg;

    void emit8(uint8_t op);
    void emit32(int v);
    int  add_constant(Value v);
    int  add_name(const char* s, int len);

    void compile_stmt(Node* s, CodeChunk* cc);
    void compile_expr(Node* e, CodeChunk* cc);
};

// 便捷入口：编译整棵程序到 chunk。失败返回 false 并写 errmsg。
bool minilang_compile(ProgramObj* prog, CodeChunk* out, String* errmsg_out);

// 自测试
int compiler_self_test();

} // namespace minilang
} // namespace nefu
