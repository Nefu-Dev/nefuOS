// ============================================================================
// nefu::minilang —— 抽象语法树节点（实现）
// ============================================================================
#include "ast.h"
#include <string.h>

namespace nefu {
namespace minilang {

// 删除一个 AstBox：先回调删除真实节点，再删 box 本身。
void minilang_ast_box_delete(void* box) {
    if (!box) return;
    AstBox* b = (AstBox*)box;
    if (b->del && b->ptr) b->del(b->ptr);
    delete b;
}

// 把字符串拷进 arena：单独 new 一块，登记成 AstBox（用 char 删除回调）。
static void delete_charbuf(void* p) { delete[] (char*)p; }

const char* ast_intern(ProgramObj* prog, const char* s, int len) {
    if (len < 0) len = (int)strlen(s);
    char* buf = new char[len + 1];
    for (int i = 0; i < len; i++) buf[i] = s[i];
    buf[len] = 0;
    AstBox* box = new AstBox();
    box->ptr = buf;
    box->del = &delete_charbuf;
    program_add_arena(prog, box);
    return buf;
}

// 显式实例化所有节点类型的删除回调，避免模板未定义错误。
template void ast_delete_thunk<LitNode>(void*);
template void ast_delete_thunk<VarNode>(void*);
template void ast_delete_thunk<BinNode>(void*);
template void ast_delete_thunk<UnaryNode>(void*);
template void ast_delete_thunk<CallNode>(void*);
template void ast_delete_thunk<AssignNode>(void*);
template void ast_delete_thunk<ArrayNode>(void*);
template void ast_delete_thunk<IndexNode>(void*);
template void ast_delete_thunk<ExprStmt>(void*);
template void ast_delete_thunk<LetStmt>(void*);
template void ast_delete_thunk<IfStmt>(void*);
template void ast_delete_thunk<WhileStmt>(void*);
template void ast_delete_thunk<ForStmt>(void*);
template void ast_delete_thunk<BlockStmt>(void*);
template void ast_delete_thunk<ReturnStmt>(void*);
template void ast_delete_thunk<FuncNode>(void*);
template void ast_delete_thunk<ProgramNode>(void*);

// 自测试：手动构造一棵小树，验证 arena 分配与回收不崩溃、字段正确。
int ast_self_test() {
    int fails = 0;

    Value progv = val_make_program();
    ProgramObj* prog = val_as_program(progv);

    // let x = 1 + 2
    LitNode* one = ast_alloc(prog, new LitNode());
    one->kind = N_LIT; one->tag = LIT_INT; one->i = 1; one->line = 1;
    LitNode* two = ast_alloc(prog, new LitNode());
    two->kind = N_LIT; two->tag = LIT_INT; two->i = 2; two->line = 1;
    BinNode* add = ast_alloc(prog, new BinNode());
    add->kind = N_BIN; add->op = OP_ADD; add->l = one; add->r = two; add->line = 1;
    LetStmt* let = ast_alloc(prog, new LetStmt());
    let->kind = N_LET; let->name = ast_intern(prog, "x", 1);
    let->namelen = 1; let->init = add; let->line = 1;

    if (let->kind != N_LET) fails++;
    if (add->op != OP_ADD) fails++;
    if (strcmp(let->name, "x") != 0) fails++;

    ProgramNode* prg = ast_alloc(prog, new ProgramNode());
    prg->kind = N_PROGRAM;
    prg->stmts.push(let);
    prog->stmts.push(let);

    if (prog->stmts.size() != 1) fails++;
    if (prg->stmts.size() != 1) fails++;

    // 释放整个 program（arena 回收）。不崩溃即通过。
    val_drop(progv);

    return fails;
}

} // namespace minilang
} // namespace nefu

