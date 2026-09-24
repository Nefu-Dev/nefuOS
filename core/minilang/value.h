// ============================================================================
// nefu::minilang —— 运行时值类型与引用计数
// ----------------------------------------------------------------------------
// 设计目标：在 bare metal（无 FPU、无 STL、无异常、无 RTTI）环境下提供一个
// 动态类型的运行时标签联合体。所有“对象型”值（字符串 / 数组 / 闭包 / 程序
// 树根 / 内置函数）都堆分配并带引用计数；标量（整数 / 定点数 / 布尔 / 空）
// 直接内联在 Value 里，无需堆分配。
//
// 引用计数规则：
//   * 每个 Value 一旦落入某个容器（数组、环境绑定、寄存器栈、闭包），
//     调用方必须对其持有一次引用（val_dup）；不再使用时 val_drop。
//   * 表达式求值“产生”的值自带一次引用所有权；赋值/传参时转移所有权，
//     必要时显式 dup。
//   * 无循环 GC：数组/闭包若形成环会泄漏，但迷你语言测试不构造环，且这与
//     裸机环境的内存模型一致（进程结束即归还）。
//
// 定点数使用 nefu::fx::fix（Q16.16），以避免依赖 FPU。
// ============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"
#include "../lib/softmath.h"

namespace nefu {
namespace minilang {

// ---- 前置声明（来自其他模块） --------------------------------------------
struct Node;            // ast.h
struct FuncNode;        // ast.h
struct Env;             // env.h
void env_retain(Env* e);
void env_release(Env* e);

// ---- 标量值类型标签 --------------------------------------------------------
enum ValType {
    V_NULL = 0,   // 空值
    V_INT,        // 整数（i 字段）
    V_FX,         // Q16.16 定点数（i 字段存 fx::fix 原始位）
    V_BOOL,       // 布尔（i 字段 0/1）
    V_OBJ         // 堆对象，具体子类型见 Obj::otype
};

// ---- 堆对象子类型标签 ------------------------------------------------------
enum ObjType {
    O_STRING = 0,   // 字符串
    O_ARRAY,        // 动态数组
    O_CLOSURE,      // 用户函数闭包（树遍历解释器用）
    O_PROGRAM,      // 一棵解析完成的 AST（arena 所有权）
    O_BUILTIN       // 内置 C 函数
};

// 内置函数签名：argc 个参数连续存于 argv[0..argc-1]，ctx 为解释器上下文
// （可空；宿主测试不依赖它）。返回值自带一次引用所有权。
struct EvalCtx;
struct Value;   // 前置声明：下面的函数指针用 Value 作返回/参数类型
typedef Value (*BuiltinFn)(int argc, Value* argv, EvalCtx* ctx);

// ---- 对象基类 --------------------------------------------------------------
struct Obj {
    int otype;      // ObjType
    int ref;        // 引用计数
};

// ---- 字符串对象 ------------------------------------------------------------
struct StrObj : Obj {
    char* s;        // 以 NUL 结尾，且 binary-safe（len 记录真实长度）
    int   len;
};

// ---- 数组对象（元素为 Value，每个元素各持一次引用） ------------------------
struct ArrObj : Obj {
    Value* items;   // 长度 len，容量 cap
    int    len;
    int    cap;
};

// ---- 闭包对象：捕获定义处环境的用户函数 ------------------------------------
struct ClosureObj : Obj {
    FuncNode* fn;       // 指向 AST（由 program arena 持有，不在此释放）
    struct ProgramObj* prog;  // 持有 program 以保证 arena 存活
    Env* env;           // 捕获的外层环境（引用计数）
};

// ---- 程序对象：一棵完整 AST 的内存所有权单元 ------------------------------
struct ProgramObj : Obj {
    List<void*> arena;   // parse 阶段 new 出来的所有节点/字符串缓冲，统一释放
    List<Node*> stmts;   // 顶层语句序列
};

// ---- 内置函数对象 ----------------------------------------------------------
struct BuiltinObj : Obj {
    const char* name;
    BuiltinFn fn;
};

// ---- 运行时值：标签联合体 --------------------------------------------------
struct Value {
    int     type = V_NULL;   // ValType
    int64_t i    = 0;        // V_INT / V_FX(原始位) / V_BOOL
    Obj*    obj  = 0;        // V_OBJ 时指向堆对象
};

// ============================================================================
// 构造器：每个都返回一个“裸引用”（ref=1），调用方持有所有权
// ============================================================================
Value val_make_null();
Value val_make_bool(bool b);
Value val_make_int(int64_t n);
Value val_make_fx(fx::fix f);          // 直接以 Q16.16 位构造
Value val_make_fx_int(int64_t n);      // 整数转定点
Value val_make_str(const char* s);
Value val_make_str_n(const char* s, int len);
Value val_make_arr(int reserve = 0);
Value val_make_closure(FuncNode* fn, ProgramObj* prog, Env* env);
Value val_make_builtin(const char* name, BuiltinFn fn);
Value val_make_program();

// ============================================================================
// 引用计数管理
// ============================================================================
Value val_dup(Value v);        // 增加一次引用，返回 v 本身
void  val_drop(Value v);       // 减少一次引用，归零时释放对象
void  obj_retain(Obj* o);
void  obj_release(Obj* o);

// ============================================================================
// 便捷访问器（不改变引用计数）
// ============================================================================
bool val_is_null(Value v);
bool val_is_truthy(Value v);            // 解释器“真值”规则
int64_t  val_as_int(Value v);           // 尽量转成整数（定点/布尔也转）
fx::fix  val_as_fx(Value v);            // 尽量转成 Q16.16
StrObj*  val_as_str(Value v);           // 若是字符串返回指针，否则 0
ArrObj*  val_as_arr(Value v);
ClosureObj* val_as_closure(Value v);
BuiltinObj* val_as_builtin(Value v);
ProgramObj* val_as_program(Value v);

const char* val_type_name(Value v);     // "int"/"fx"/"str"/"arr"/"fn"/"null"/"bool"

// 把任意值转成一个新字符串对象（返回裸引用），用于 print / str()
StrObj* val_to_strobj(Value v);

// ============================================================================
// 数组操作（原地修改 arr 对象；越界返回 false）
// ============================================================================
bool arr_push(ArrObj* a, Value v);      // v 被 dup 后存入
bool arr_set(ArrObj* a, int idx, Value v); // v 被 dup 后存入，旧值 drop
Value arr_get(ArrObj* a, int idx);      // 返回 dup 后的引用（调用方负责 drop）
int  arr_len(ArrObj* a);

// ============================================================================
// 程序 arena：把 parse 阶段分配的节点登记进去，最后统一释放
// ============================================================================
void program_add_arena(ProgramObj* p, void* ptr);
void program_free(ProgramObj* p);       // 释放 arena 全部内容 + 自身

// ============================================================================
// 自测试：返回失败数（0 表示全部通过）
// ============================================================================
int value_self_test();

} // namespace minilang
} // namespace nefu
