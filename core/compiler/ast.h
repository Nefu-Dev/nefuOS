// ============================================================================
// nefu::compiler —— 抽象语法树（ast.h）
// ----------------------------------------------------------------------------
// 所有节点从一个 bump arena（CompContext）分配，整棵树随上下文一次性回收，
// 不使用 RTTI：第一个字段是 kind，靠它做向下转型。
// 包含：节点种类、类型系统、arena 分配器、pretty printer。
// ============================================================================
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace compiler {

// ---- 类型系统 ----
enum TyKind {
    TY_VOID = 0,
    TY_INT,        // 32 位整型
    TY_CHAR,       // 8 位字符
    TY_LONG,       // 64 位整型
    TY_PTR,        // 指针（base 指向被指类型）
    TY_FUNC        // 函数（返回类型 + 参数个数）
};

struct Type {
    int kind;          // TyKind
    Type* base;        // TY_PTR 指向的基类型；TY_FUNC 的返回类型
    int   ptrlevel;    // 指针层数（int** -> 2）
    int   argc;        // TY_FUNC 的参数个数
};

// ---- 节点种类 ----
enum Nk {
    N_PROGRAM = 0,   // 整棵翻译单元
    N_FUNC,          // 函数定义
    N_VARDECL,      // 变量声明 / 定义
    N_BLOCK,         // 复合语句
    N_IF,            // if / else
    N_WHILE,        // while
    N_FOR,          // for
    N_RETURN,        // return
    N_BREAK,         // break
    N_CONTINUE,      // continue
    N_EXPRSTMT,      // 表达式语句
    // ---- 表达式 ----
    N_LITINT,        // 整数字面量
    N_LITCHAR,       // 字符字面量
    N_LITSTR,        // 字符串字面量
    N_VAR,          // 变量引用
    N_BIN,          // 二元运算
    N_UN,           // 一元运算
    N_ASSIGN,       // 赋值（含复合赋值）
    N_CALL,         // 函数调用
    N_INDEX,        // 下标 a[b]
    N_CAST          // 类型转换
};

// ---- 运算符（词法 token 映射到语义 op）----
enum Op {
    OP_ADD = 0, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
    OP_ASSIGN, OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN,
    OP_NEG, OP_NOT, OP_BITNOT, OP_ADDR, OP_DEREF
};

// ---- 节点基类 ----
struct Node {
    int kind;       // Nk
    int line;       // 源码行号
};

// ---- 表达式节点 ----
struct LitIntNode : Node { int64_t value; };
struct LitCharNode : Node { int value; };
struct LitStrNode : Node { const char* text; int len; };
struct VarNode : Node { const char* name; int namelen; };

struct BinNode : Node { int op; Node* l; Node* r; };
struct UnNode : Node { int op; Node* e; };

struct AssignNode : Node {
    int op;             // OP_ASSIGN / 复合赋值
    Node* target;
    Node* value;
};

struct CallNode : Node { Node* callee; List<Node*> args; };
struct IndexNode : Node { Node* obj; Node* idx; };
struct CastNode : Node { Type* type; Node* e; };

// ---- 语句节点 ----
struct ExprStmt : Node { Node* e; };

struct VarDecl : Node {
    Type* type;
    const char* name;
    int namelen;
    Node* init;         // 可空
};

struct Block : Node { List<Node*> stmts; };

struct IfStmt : Node { Node* cond; Node* thenb; Node* elseb; };
struct WhileStmt : Node { Node* cond; Node* body; };
struct ForStmt : Node { Node* init; Node* cond; Node* post; Node* body; };
struct ReturnStmt : Node { Node* e; };

struct FuncNode : Node {
    Type* type;             // TY_FUNC
    const char* name;
    int namelen;
    List<const char*> param_names;
    Block* body;            // 可空（声明）
};

struct ProgramNode : Node { List<Node*> decls; };

// ---- 编译上下文：bump arena（多 chunk，指针永不移动）+ 字符串驻留 ----
struct CompContext {
    uint8_t* arena;         // 当前 chunk
    int arena_cap;
    int arena_pos;
    List<uint8_t*> chunks;  // 历史 chunk（析构时统一回收）
    String err;             // 错误信息
    int err_count;

    CompContext();
    ~CompContext();
    void reset();
    void* alloc(int bytes);             // 对齐 16 的 bump 分配（不移动已有指针）
    const char* dup(const char* s, int len);
    Type* make_type(int kind, Type* base = 0, int argc = 0);
    void error_at(int line, const char* msg);
};

// ---- 工厂函数（在 arena 上分配节点）----
// 必须用 placement new 触发默认构造：含 List/String 成员的节点（Block/CallNode/
// ProgramNode/FuncNode 等）若只裸写内存，其 List 成员为垃圾值，push 会写野指针。
template <typename T> T* mknode(CompContext& cc, int line) {
    void* p = cc.alloc(sizeof(T));
    T* n = new (p) T();     // 价值初始化：POD 清零、List/String 成员走默认构造
    n->kind = 0; n->line = line;
    return n;
}

ProgramNode*  make_program(CompContext& cc);
FuncNode*     make_func(CompContext& cc, Type* ty, const char* name, int namelen);
VarDecl*      make_vardecl(CompContext& cc, Type* ty, const char* name, int namelen, Node* init);
Block*        make_block(CompContext& cc);
IfStmt*       make_if(CompContext& cc, Node* cond, Node* thenb, Node* elseb);
WhileStmt*    make_while(CompContext& cc, Node* cond, Node* body);
ForStmt*      make_for(CompContext& cc, Node* init, Node* cond, Node* post, Node* body);
ReturnStmt*   make_return(CompContext& cc, Node* e);
ExprStmt*     make_exprstmt(CompContext& cc, Node* e);

LitIntNode*   make_litint(CompContext& cc, int64_t v);
LitCharNode*  make_litchar(CompContext& cc, int v);
LitStrNode*   make_litstr(CompContext& cc, const char* s, int len);
VarNode*      make_var(CompContext& cc, const char* name, int namelen);
BinNode*      make_bin(CompContext& cc, int op, Node* l, Node* r);
UnNode*       make_un(CompContext& cc, int op, Node* e);
AssignNode*   make_assign(CompContext& cc, int op, Node* t, Node* v);
CallNode*     make_call(CompContext& cc, Node* callee);
IndexNode*    make_index(CompContext& cc, Node* obj, Node* idx);

// ---- pretty printer：把 AST 写成缩进文本 ----
void ast_dump(String& out, Node* root);
const char* op_name(int op);
const char* ty_name(Type* t);

// 递归统计 AST 节点总数
int ast_count_nodes(Node* root);

// 校验 AST：检查关键字段非空，返回错误数（0 = 合法）
int ast_validate(Node* root);

// AST 自测试：返回失败数
int ast_self_test();

} // namespace compiler
} // namespace nefu
