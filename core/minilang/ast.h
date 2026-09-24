// ============================================================================
// nefu::minilang —— 抽象语法树节点（ast.h）
// ----------------------------------------------------------------------------
// 所有节点都是 final 结构体，第一个字段是 kind（NodeKind），靠它做向下转型，
// 不使用 RTTI。节点由 parser 用 ast_alloc() 分配，并登记进 ProgramObj 的
// arena，program_free 时统一回收（见 AstBox）。
// ============================================================================
#pragma once

#include "../klib/klib.h"
#include "value.h"

namespace nefu {
namespace minilang {

// 节点种类
enum NodeKind {
    // 表达式
    N_LIT = 0,      // 字面量
    N_VAR,          // 变量引用
    N_BIN,          // 二元运算
    N_UNARY,        // 一元运算
    N_CALL,         // 函数调用
    N_ASSIGN,       // 赋值（=）
    N_ARRAY,        // 数组字面量
    N_INDEX,        // 下标访问 arr[i]
    // 语句
    N_EXPR_STMT,    // 表达式语句
    N_LET,          // let 声明
    N_IF,           // if/else
    N_WHILE,        // while
    N_FOR,          // for
    N_BLOCK,        // 块
    N_RETURN,       // return
    N_FUNC,         // 函数定义
    N_PROGRAM       // 整棵程序
};

// 字面量子类型
enum LitTag { LIT_NULL, LIT_BOOL, LIT_INT, LIT_FLOAT, LIT_STR };

// 二元 / 一元运算符（词法 token 映射过来的语义 op）
enum Op {
    OP_ADD = 0, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NEG, OP_NOT
};

// 节点基类
struct Node {
    int kind;       // NodeKind
    int line;
};

// ---- 表达式节点 ----
struct LitNode : Node {
    int tag;            // LitTag
    int64_t  i;         // LIT_INT
    fx::fix  f;         // LIT_FLOAT
    const char* s;      // LIT_STR（arena 拥有）
};

struct VarNode : Node {
    const char* name;   // arena 拥有
    int   namelen;
};

struct BinNode : Node {
    int op;             // Op
    Node* l;
    Node* r;
};

struct UnaryNode : Node {
    int op;             // OP_NEG / OP_NOT
    Node* e;
};

struct CallNode : Node {
    Node* callee;
    List<Node*> args;
};

struct AssignNode : Node {
    Node* target;       // VarNode 或 IndexNode
    Node* value;
};

struct ArrayNode : Node {
    List<Node*> elems;
};

struct IndexNode : Node {
    Node* obj;
    Node* idx;
};

// ---- 语句节点 ----
struct ExprStmt : Node { Node* e; };

struct LetStmt : Node {
    const char* name;
    int namelen;
    Node* init;
};

struct IfStmt : Node {
    Node* cond;
    Node* thenb;
    Node* elseb;        // 可空
};

struct WhileStmt : Node {
    Node* cond;
    Node* body;
};

struct ForStmt : Node {
    Node* init;         // 可空（LetStmt 或 ExprStmt）
    Node* cond;         // 可空
    Node* post;         // 可空（表达式）
    Node* body;
};

struct BlockStmt : Node {
    List<Node*> stmts;
};

struct ReturnStmt : Node {
    Node* e;            // 可空
};

struct FuncNode : Node {
    const char* name;
    int namelen;
    List<const char*> params;   // 参数名（arena 拥有）
    List<int>         paramlen;
    Node* body;                 // BlockStmt
};

struct ProgramNode : Node {
    List<Node*> stmts;
};

// ---- arena 分配器 ----
// AstBox 把节点指针和它的删除回调打包，统一放进 ProgramObj::arena。
struct AstBox {
    void (*del)(void*);
    void* ptr;
};

// 由 program_free 调用：删除 box 内真实节点，再删 box 自身。
void minilang_ast_box_delete(void* box);

// 模板删除回调（在 ast.cpp 里显式实例化常用类型）
template <typename T>
void ast_delete_thunk(void* p) { delete (T*)p; }

// 分配一个节点并登记进 program arena，返回节点指针。
template <typename T>
T* ast_alloc(ProgramObj* prog, T* node) {
    AstBox* box = new AstBox();
    box->ptr = node;
    box->del  = &ast_delete_thunk<T>;
    program_add_arena(prog, box);
    return node;
}

// 把一段 C 字符串拷贝进 arena（返回 arena 拥有的副本）
const char* ast_intern(ProgramObj* prog, const char* s, int len);

// AST 自测试：返回失败数
int ast_self_test();

} // namespace minilang
} // namespace nefu
