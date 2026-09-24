// ============================================================================
// nefu::minilang —— AST 到字节码编译器（实现）
// ============================================================================
#include "compiler.h"
#include "parser.h"
#include <string.h>

namespace nefu {
namespace minilang {

static void emit8(CodeChunk* cc, uint8_t op) { cc->code.push(op); }

static void emit32(CodeChunk* cc, int v) {
    cc->code.push((uint8_t)(v & 0xFF));
    cc->code.push((uint8_t)((v >> 8) & 0xFF));
    cc->code.push((uint8_t)((v >> 16) & 0xFF));
    cc->code.push((uint8_t)((v >> 24) & 0xFF));
}

static int add_constant(CodeChunk* cc, Value v) {
    cc->consts.push(v);
    return cc->consts.size() - 1;
}

static int add_name(CodeChunk* cc, const char* s) {
    for (int i = 0; i < cc->names.size(); i++) {
        if (strcmp(cc->names[i], s) == 0) return i;
    }
    cc->names.push(s);
    return cc->names.size() - 1;
}

static void compile_stmt(CodeChunk* cc, Node* s, bool keep_last);
static void compile_expr(CodeChunk* cc, Node* e);

static int arith_opcode(int op) {
    switch (op) {
    case OP_ADD: return BC_ADD;
    case OP_SUB: return BC_SUB;
    case OP_MUL: return BC_MUL;
    case OP_DIV: return BC_DIV;
    case OP_MOD: return BC_MOD;
    case OP_EQ:  return BC_EQ;
    case OP_NEQ: return BC_NEQ;
    case OP_LT:  return BC_LT;
    case OP_GT:  return BC_GT;
    case OP_LE:  return BC_LE;
    case OP_GE:  return BC_GE;
    }
    return -1;
}

// ---------------- 表达式编译 ----------------
static void compile_expr(CodeChunk* cc, Node* e) {
    if (!e) { emit8(cc, BC_NULL); return; }
    switch (e->kind) {
    case N_LIT: {
        LitNode* n = (LitNode*)e;
        if (n->tag == LIT_INT) {
            int idx = add_constant(cc, val_make_int(n->i));
            emit8(cc, BC_CONST); emit32(cc, idx);
        } else if (n->tag == LIT_FLOAT) {
            int idx = add_constant(cc, val_make_fx(n->f));
            emit8(cc, BC_CONST); emit32(cc, idx);
        } else if (n->tag == LIT_STR) {
            int idx = add_constant(cc, val_make_str(n->s));
            emit8(cc, BC_CONST); emit32(cc, idx);
        } else if (n->tag == LIT_BOOL) {
            emit8(cc, n->i ? BC_TRUE : BC_FALSE);
        } else {
            emit8(cc, BC_NULL);
        }
        break;
    }
    case N_VAR: {
        VarNode* v = (VarNode*)e;
        emit8(cc, BC_LOAD_NAME);
        emit32(cc, add_name(cc, v->name));
        break;
    }
    case N_BIN: {
        BinNode* b = (BinNode*)e;
        if (b->op == OP_AND) {
            compile_expr(cc, b->l);
            int jz = cc->code.size();
            emit8(cc, BC_JZ); emit32(cc, 0);  // 占位
            compile_expr(cc, b->r);
            // patch jz 跳到这里（r 之后）
            int here = cc->code.size();
            cc->code[jz + 1] = (uint8_t)(here & 0xFF);
            cc->code[jz + 2] = (uint8_t)((here >> 8) & 0xFF);
            cc->code[jz + 3] = (uint8_t)((here >> 16) & 0xFF);
            cc->code[jz + 4] = (uint8_t)((here >> 24) & 0xFF);
            break;
        }
        if (b->op == OP_OR) {
            compile_expr(cc, b->l);
            int jz = cc->code.size();
            // or: 若真则跳过右值——用 JZ 反转：先判假才求值右
            emit8(cc, BC_JZ); emit32(cc, 0);
            compile_expr(cc, b->r);
            int here = cc->code.size();
            cc->code[jz + 1] = (uint8_t)(here & 0xFF);
            cc->code[jz + 2] = (uint8_t)((here >> 8) & 0xFF);
            cc->code[jz + 3] = (uint8_t)((here >> 16) & 0xFF);
            cc->code[jz + 4] = (uint8_t)((here >> 24) & 0xFF);
            break;
        }
        compile_expr(cc, b->l);
        compile_expr(cc, b->r);
        int opc = arith_opcode(b->op);
        emit8(cc, (uint8_t)opc);
        break;
    }
    case N_UNARY: {
        UnaryNode* u = (UnaryNode*)e;
        compile_expr(cc, u->e);
        emit8(cc, u->op == OP_NEG ? BC_NEG : BC_NOT);
        break;
    }
    case N_ARRAY: {
        ArrayNode* a = (ArrayNode*)e;
        for (int i = 0; i < a->elems.size(); i++) compile_expr(cc, a->elems[i]);
        emit8(cc, BC_ARRAY); emit32(cc, a->elems.size());
        break;
    }
    case N_INDEX: {
        IndexNode* ix = (IndexNode*)e;
        compile_expr(cc, ix->obj);
        compile_expr(cc, ix->idx);
        emit8(cc, BC_INDEX);
        break;
    }
    case N_ASSIGN: {
        AssignNode* a = (AssignNode*)e;
        compile_expr(cc, a->value);
        if (a->target->kind == N_VAR) {
            VarNode* t = (VarNode*)a->target;
            emit8(cc, BC_STORE_NAME);
            emit32(cc, add_name(cc, t->name));
        } else if (a->target->kind == N_INDEX) {
            IndexNode* ix = (IndexNode*)a->target;
            compile_expr(cc, ix->obj);
            compile_expr(cc, ix->idx);
            emit8(cc, BC_SET_INDEX);
        }
        break;
    }
    case N_CALL: {
        CallNode* c = (CallNode*)e;
        compile_expr(cc, c->callee);
        for (int i = 0; i < c->args.size(); i++) compile_expr(cc, c->args[i]);
        emit8(cc, BC_CALL); emit32(cc, c->args.size());
        break;
    }
    default:
        emit8(cc, BC_NULL);
        break;
    }
}

// ---------------- 语句编译 ----------------
static void compile_func_body(CodeChunk* cc, FuncNode* fn);

static void compile_stmt(CodeChunk* cc, Node* s, bool keep_last) {
    if (!s) return;
    switch (s->kind) {
    case N_EXPR_STMT: {
        ExprStmt* es = (ExprStmt*)s;
        compile_expr(cc, es->e);
        if (!keep_last) emit8(cc, BC_POP);
        break;
    }
    case N_LET: {
        LetStmt* ls = (LetStmt*)s;
        if (ls->init) compile_expr(cc, ls->init);
        else emit8(cc, BC_NULL);
        emit8(cc, BC_DEF_NAME);
        emit32(cc, add_name(cc, ls->name));
        break;
    }
    case N_BLOCK: {
        BlockStmt* b = (BlockStmt*)s;
        for (int i = 0; i < b->stmts.size(); i++) compile_stmt(cc, b->stmts[i], false);
        break;
    }
    case N_IF: {
        IfStmt* is = (IfStmt*)s;
        compile_expr(cc, is->cond);
        int jz = cc->code.size();
        emit8(cc, BC_JZ); emit32(cc, 0);
        compile_stmt(cc, is->thenb, false);
        int jmpend = -1;
        if (is->elseb) {
            jmpend = cc->code.size();
            emit8(cc, BC_JMP); emit32(cc, 0);
        }
        int elsepos = cc->code.size();
        cc->code[jz+1] = (uint8_t)(elsepos & 0xFF);
        cc->code[jz+2] = (uint8_t)((elsepos >> 8) & 0xFF);
        cc->code[jz+3] = (uint8_t)((elsepos >> 16) & 0xFF);
        cc->code[jz+4] = (uint8_t)((elsepos >> 24) & 0xFF);
        if (is->elseb) {
            compile_stmt(cc, is->elseb, false);
            int endpos = cc->code.size();
            cc->code[jmpend+1] = (uint8_t)(endpos & 0xFF);
            cc->code[jmpend+2] = (uint8_t)((endpos >> 8) & 0xFF);
            cc->code[jmpend+3] = (uint8_t)((endpos >> 16) & 0xFF);
            cc->code[jmpend+4] = (uint8_t)((endpos >> 24) & 0xFF);
        }
        break;
    }
    case N_WHILE: {
        WhileStmt* ws = (WhileStmt*)s;
        int loop = cc->code.size();
        compile_expr(cc, ws->cond);
        int jz = cc->code.size();
        emit8(cc, BC_JZ); emit32(cc, 0);
        compile_stmt(cc, ws->body, false);
        emit8(cc, BC_JMP); emit32(cc, loop);
        int endpos = cc->code.size();
        cc->code[jz+1] = (uint8_t)(endpos & 0xFF);
        cc->code[jz+2] = (uint8_t)((endpos >> 8) & 0xFF);
        cc->code[jz+3] = (uint8_t)((endpos >> 16) & 0xFF);
        cc->code[jz+4] = (uint8_t)((endpos >> 24) & 0xFF);
        break;
    }
    case N_FOR: {
        ForStmt* fs = (ForStmt*)s;
        if (fs->init) compile_stmt(cc, fs->init, false);
        int loop = cc->code.size();
        if (fs->cond) compile_expr(cc, fs->cond);
        else emit8(cc, BC_TRUE);
        int jz = cc->code.size();
        emit8(cc, BC_JZ); emit32(cc, 0);
        compile_stmt(cc, fs->body, false);
        if (fs->post) compile_expr(cc, fs->post);
        emit8(cc, BC_POP);
        emit8(cc, BC_JMP); emit32(cc, loop);
        int endpos = cc->code.size();
        cc->code[jz+1] = (uint8_t)(endpos & 0xFF);
        cc->code[jz+2] = (uint8_t)((endpos >> 8) & 0xFF);
        cc->code[jz+3] = (uint8_t)((endpos >> 16) & 0xFF);
        cc->code[jz+4] = (uint8_t)((endpos >> 24) & 0xFF);
        break;
    }
    case N_RETURN: {
        ReturnStmt* rs = (ReturnStmt*)s;
        if (rs->e) compile_expr(cc, rs->e);
        else emit8(cc, BC_NULL);
        emit8(cc, BC_RET);
        break;
    }
    case N_FUNC: {
        FuncNode* fn = (FuncNode*)s;
        FuncProto fp;
        fp.entry = 0;
        fp.nargs = fn->params.size();
        fp.name = fn->name; fp.fn = fn;
        cc->funcs.push(fp);
        int pidx = cc->funcs.size() - 1;
        emit8(cc, BC_CLOSURE); emit32(cc, pidx);
        emit8(cc, BC_DEF_NAME); emit32(cc, add_name(cc, fn->name));
        break;
    }
    default: break;
    }
}

// 编译函数体：登记入口，编译体，最后 emit RET
static void compile_func_body(CodeChunk* cc, FuncNode* fn) {
    // 找到对应的 proto（按名字+参数）
    int pidx = -1;
    for (int i = 0; i < cc->funcs.size(); i++) {
        if (cc->funcs[i].name == fn->name ||
            (cc->funcs[i].name && fn->name && strcmp(cc->funcs[i].name, fn->name) == 0)) {
            pidx = i; break;
        }
    }
    int entry = cc->code.size();
    if (pidx >= 0) cc->funcs[pidx].entry = entry;
    // 参数在 VM 里已绑定到新环境；这里直接编译体
    compile_stmt(cc, fn->body, false);
    emit8(cc, BC_NULL);
    emit8(cc, BC_RET);
}

// 递归遍历语句树，收集所有函数定义（含嵌套函数）
static void walk_collect_funcs(List<Node*>* out, Node* s) {
    if (!s) return;
    switch (s->kind) {
    case N_FUNC:
        out->push(s);
        walk_collect_funcs(out, ((FuncNode*)s)->body);
        break;
    case N_BLOCK: {
        BlockStmt* b = (BlockStmt*)s;
        for (int i = 0; i < b->stmts.size(); i++) walk_collect_funcs(out, b->stmts[i]);
        break;
    }
    case N_IF: {
        IfStmt* is = (IfStmt*)s;
        walk_collect_funcs(out, is->thenb);
        walk_collect_funcs(out, is->elseb);
        break;
    }
    case N_WHILE:
        walk_collect_funcs(out, ((WhileStmt*)s)->body);
        break;
    case N_FOR:
        walk_collect_funcs(out, ((ForStmt*)s)->body);
        break;
    default: break;
    }
}
// 编译整棵程序
bool minilang_compile(ProgramObj* prog, CodeChunk* out, String* errmsg_out) {
    out->code.clear();
    for (int _i=0;_i<out->consts.size();_i++) val_drop(out->consts[_i]); out->consts.clear();
    out->names.clear();
    out->funcs.clear();
    out->main_entry = 0;

    // 第一遍：编译顶层语句（函数定义只发 CLOSURE+DEF）
    int nstmts = prog->stmts.size();
    for (int i = 0; i < nstmts; i++) {
        Node* s = prog->stmts[i];
        bool keep = (i == nstmts - 1 && s->kind == N_EXPR_STMT);
        compile_stmt(out, s, keep);
    }
    // 顶层收尾：压 null 并返回（若最后一条是表达式语句，其值已留在栈上，直接 RET）
    bool last_kept = (nstmts > 0 && prog->stmts[nstmts-1]->kind == N_EXPR_STMT);
    if (!last_kept) emit8(out, BC_NULL);
    emit8(out, BC_RET);

    // 第二遍：递归编译所有函数体（含嵌套函数），追加在末尾
    List<Node*> allfuncs;
    for (int i = 0; i < nstmts; i++) walk_collect_funcs(&allfuncs, prog->stmts[i]);
    for (int i = 0; i < allfuncs.size(); i++)
        compile_func_body(out, (FuncNode*)allfuncs[i]);
    return true;
}

// 自测试：解析一段代码并编译，检查字节码非空、常量数合理。
int compiler_self_test() {
    int fails = 0;
    String err;
    Value progv = minilang_parse("fn fib(n){ if(n<2){return n;} return fib(n-1)+fib(n-2);} fib(10);", &err);
    if (val_is_null(progv)) return 1;
    ProgramObj* prog = val_as_program(progv);
    CodeChunk cc;
    if (!minilang_compile(prog, &cc, &err)) { fails++; }
    if (cc.code.size() < 10) fails++;
    if (cc.funcs.size() != 1) fails++;
    // 常量池应至少有 2（2 与 10）
    if (cc.consts.size() < 2) fails++;
    // 释放常量池里的值
    for (int i = 0; i < cc.consts.size(); i++) val_drop(cc.consts[i]);
    val_drop(progv);
    return fails;
}

} // namespace minilang
} // namespace nefu







