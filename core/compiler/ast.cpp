// ============================================================================
// nefu::compiler —— AST 实现（ast.cpp）
// ============================================================================
#include "ast.h"
#include "../platform.h"
#include <stdarg.h>

namespace nefu {
namespace compiler {

// ---- 上下文：多 chunk bump arena（绝不移动已有指针）----
CompContext::CompContext() : arena(0), arena_cap(0), arena_pos(0), err_count(0) {
    arena_cap = 1 << 16;             // 64KB 初始 chunk
    arena = (uint8_t*)kalloc(arena_cap);
}
CompContext::~CompContext() {
    // 释放所有历史 chunk 与当前 chunk
    for (int i = 0; i < chunks.size(); i++) kfree(chunks[i]);
    if (arena) kfree(arena);
    arena = 0;
}
void CompContext::reset() {
    arena_pos = 0;
    err.clear();
    err_count = 0;
}
void* CompContext::alloc(int bytes) {
    // 16 字节对齐；当前 chunk 不够就开新 chunk（旧块保留，指针稳定不悬空）
    int aligned = (bytes + 15) & ~15;
    if (arena_pos + aligned > arena_cap) {
        chunks.push(arena);             // 旧 chunk 挂链表，析构时统一回收
        arena_cap = 1 << 16;
        arena = (uint8_t*)kalloc(arena_cap);
        arena_pos = 0;
    }
    void* p = arena + arena_pos;
    arena_pos += aligned;
    return p;
}
const char* CompContext::dup(const char* s, int len) {
    char* p = (char*)alloc(len + 1);
    for (int i = 0; i < len; i++) p[i] = s[i];
    p[len] = 0;
    return p;
}
Type* CompContext::make_type(int kind, Type* base, int argc) {
    Type* t = (Type*)alloc(sizeof(Type));
    t->kind = kind; t->base = base; t->ptrlevel = 0; t->argc = argc;
    return t;
}
void CompContext::error_at(int line, const char* msg) {
    err_count++;
    char buf[128];
    ksprintf(buf, sizeof(buf), "line %d: %s\n", line, msg);
    err += buf;
}

// ---- 工厂 ----
ProgramNode* make_program(CompContext& cc) {
    ProgramNode* n = mknode<ProgramNode>(cc, 1);
    n->kind = N_PROGRAM;
    return n;
}
FuncNode* make_func(CompContext& cc, Type* ty, const char* name, int namelen) {
    FuncNode* n = mknode<FuncNode>(cc, 1);
    n->kind = N_FUNC;
    n->type = ty;
    n->name = cc.dup(name, namelen);
    n->namelen = namelen;
    n->body = 0;
    return n;
}
VarDecl* make_vardecl(CompContext& cc, Type* ty, const char* name, int namelen, Node* init) {
    VarDecl* n = mknode<VarDecl>(cc, 1);
    n->kind = N_VARDECL;
    n->type = ty;
    n->name = cc.dup(name, namelen);
    n->namelen = namelen;
    n->init = init;
    return n;
}
Block* make_block(CompContext& cc) {
    Block* n = mknode<Block>(cc, 1);
    n->kind = N_BLOCK;
    return n;
}
IfStmt* make_if(CompContext& cc, Node* cond, Node* thenb, Node* elseb) {
    IfStmt* n = mknode<IfStmt>(cc, 1);
    n->kind = N_IF; n->cond = cond; n->thenb = thenb; n->elseb = elseb;
    return n;
}
WhileStmt* make_while(CompContext& cc, Node* cond, Node* body) {
    WhileStmt* n = mknode<WhileStmt>(cc, 1);
    n->kind = N_WHILE; n->cond = cond; n->body = body;
    return n;
}
ForStmt* make_for(CompContext& cc, Node* init, Node* cond, Node* post, Node* body) {
    ForStmt* n = mknode<ForStmt>(cc, 1);
    n->kind = N_FOR; n->init = init; n->cond = cond; n->post = post; n->body = body;
    return n;
}
ReturnStmt* make_return(CompContext& cc, Node* e) {
    ReturnStmt* n = mknode<ReturnStmt>(cc, 1);
    n->kind = N_RETURN; n->e = e;
    return n;
}
ExprStmt* make_exprstmt(CompContext& cc, Node* e) {
    ExprStmt* n = mknode<ExprStmt>(cc, 1);
    n->kind = N_EXPRSTMT; n->e = e;
    return n;
}
LitIntNode* make_litint(CompContext& cc, int64_t v) {
    LitIntNode* n = mknode<LitIntNode>(cc, 1);
    n->kind = N_LITINT; n->value = v;
    return n;
}
LitCharNode* make_litchar(CompContext& cc, int v) {
    LitCharNode* n = mknode<LitCharNode>(cc, 1);
    n->kind = N_LITCHAR; n->value = v;
    return n;
}
LitStrNode* make_litstr(CompContext& cc, const char* s, int len) {
    LitStrNode* n = mknode<LitStrNode>(cc, 1);
    n->kind = N_LITSTR; n->text = cc.dup(s, len); n->len = len;
    return n;
}
VarNode* make_var(CompContext& cc, const char* name, int namelen) {
    VarNode* n = mknode<VarNode>(cc, 1);
    n->kind = N_VAR; n->name = cc.dup(name, namelen); n->namelen = namelen;
    return n;
}
BinNode* make_bin(CompContext& cc, int op, Node* l, Node* r) {
    BinNode* n = mknode<BinNode>(cc, 1);
    n->kind = N_BIN; n->op = op; n->l = l; n->r = r;
    return n;
}
UnNode* make_un(CompContext& cc, int op, Node* e) {
    UnNode* n = mknode<UnNode>(cc, 1);
    n->kind = N_UN; n->op = op; n->e = e;
    return n;
}
AssignNode* make_assign(CompContext& cc, int op, Node* t, Node* v) {
    AssignNode* n = mknode<AssignNode>(cc, 1);
    n->kind = N_ASSIGN; n->op = op; n->target = t; n->value = v;
    return n;
}
CallNode* make_call(CompContext& cc, Node* callee) {
    CallNode* n = mknode<CallNode>(cc, 1);
    n->kind = N_CALL; n->callee = callee;
    return n;
}
IndexNode* make_index(CompContext& cc, Node* obj, Node* idx) {
    IndexNode* n = mknode<IndexNode>(cc, 1);
    n->kind = N_INDEX; n->obj = obj; n->idx = idx;
    return n;
}

// ---- 名字表 ----
const char* op_name(int op) {
    switch (op) {
        case OP_ADD: return "+"; case OP_SUB: return "-";
        case OP_MUL: return "*"; case OP_DIV: return "/"; case OP_MOD: return "%";
        case OP_EQ: return "=="; case OP_NE: return "!=";
        case OP_LT: return "<"; case OP_GT: return ">";
        case OP_LE: return "<="; case OP_GE: return ">=";
        case OP_AND: return "&"; case OP_OR: return "|"; case OP_XOR: return "^";
        case OP_SHL: return "<<"; case OP_SHR: return ">>";
        case OP_ASSIGN: return "=";
        case OP_ADD_ASSIGN: return "+="; case OP_SUB_ASSIGN: return "-=";
        case OP_MUL_ASSIGN: return "*="; case OP_DIV_ASSIGN: return "/=";
        case OP_NEG: return "-u"; case OP_NOT: return "!"; case OP_BITNOT: return "~";
        case OP_ADDR: return "&"; case OP_DEREF: return "*";
    }
    return "?";
}
const char* ty_name(Type* t) {
    if (!t) return "<null>";
    switch (t->kind) {
        case TY_VOID: return "void";
        case TY_INT: return "int";
        case TY_CHAR: return "char";
        case TY_LONG: return "long";
        case TY_PTR: return "ptr";
        case TY_FUNC: return "func";
    }
    return "?";
}

// ---- pretty printer ----
static void dump_indent(String& out, int depth) {
    for (int i = 0; i < depth; i++) out += "  ";
}
static void dump_node(String& out, Node* n, int depth);

static void dump_list_stmts(String& out, List<Node*>& stmts, int depth) {
    for (int i = 0; i < stmts.size(); i++) dump_node(out, stmts[i], depth);
}

static void dump_node(String& out, Node* n, int depth) {
    if (!n) { out += "(null)\n"; return; }
    char buf[96];
    switch (n->kind) {
        case N_PROGRAM: {
            ProgramNode* p = (ProgramNode*)n;
            out += "(program\n";
            for (int i = 0; i < p->decls.size(); i++) dump_node(out, p->decls[i], depth + 1);
            out += ")\n";
            break;
        }
        case N_FUNC: {
            FuncNode* f = (FuncNode*)n;
            ksprintf(buf, sizeof(buf), "(func %s returns=%s\n", f->name, ty_name(f->type ? f->type->base : 0));
            dump_indent(out, depth); out += buf;
            if (f->body) dump_node(out, (Node*)f->body, depth + 1);
            out += ")\n";
            break;
        }
        case N_VARDECL: {
            VarDecl* v = (VarDecl*)n;
            dump_indent(out, depth);
            ksprintf(buf, sizeof(buf), "(var %s : %s", v->name, ty_name(v->type));
            out += buf;
            if (v->init) { out += " = "; dump_node(out, v->init, 0); }
            out += ")\n";
            break;
        }
        case N_BLOCK: {
            Block* b = (Block*)n;
            dump_indent(out, depth); out += "(block\n";
            dump_list_stmts(out, b->stmts, depth + 1);
            dump_indent(out, depth); out += ")\n";
            break;
        }
        case N_IF: {
            IfStmt* s = (IfStmt*)n;
            dump_indent(out, depth); out += "(if "; dump_node(out, s->cond, 0);
            dump_node(out, s->thenb, depth + 1);
            if (s->elseb) dump_node(out, s->elseb, depth + 1);
            out += ")\n";
            break;
        }
        case N_WHILE: {
            WhileStmt* s = (WhileStmt*)n;
            dump_indent(out, depth); out += "(while "; dump_node(out, s->cond, 0);
            dump_node(out, s->body, depth + 1); out += ")\n";
            break;
        }
        case N_FOR: {
            ForStmt* s = (ForStmt*)n;
            dump_indent(out, depth); out += "(for\n";
            if (s->init) dump_node(out, s->init, depth + 1);
            if (s->cond) dump_node(out, s->cond, depth + 1);
            if (s->post) dump_node(out, s->post, depth + 1);
            dump_node(out, s->body, depth + 1); out += ")\n";
            break;
        }
        case N_RETURN: {
            ReturnStmt* s = (ReturnStmt*)n;
            dump_indent(out, depth); out += "(return ";
            if (s->e) dump_node(out, s->e, 0);
            out += ")\n";
            break;
        }
        case N_BREAK: dump_indent(out, depth); out += "(break)\n"; break;
        case N_CONTINUE: dump_indent(out, depth); out += "(continue)\n"; break;
        case N_EXPRSTMT: {
            ExprStmt* s = (ExprStmt*)n;
            dump_indent(out, depth); dump_node(out, s->e, 0); out += "\n";
            break;
        }
        case N_LITINT: {
            LitIntNode* l = (LitIntNode*)n;
            ksprintf(buf, sizeof(buf), "%d", (int)l->value);
            out += buf; break;
        }
        case N_LITCHAR: {
            LitCharNode* l = (LitCharNode*)n;
            ksprintf(buf, sizeof(buf), "'%d'", l->value);
            out += buf; break;
        }
        case N_LITSTR: {
            LitStrNode* l = (LitStrNode*)n;
            out += "\""; out += l->text; out += "\""; break;
        }
        case N_VAR: {
            VarNode* v = (VarNode*)n;
            out += v->name; break;
        }
        case N_BIN: {
            BinNode* b = (BinNode*)n;
            out += "("; out += op_name(b->op); out += " ";
            dump_node(out, b->l, 0); out += " ";
            dump_node(out, b->r, 0); out += ")";
            break;
        }
        case N_UN: {
            UnNode* u = (UnNode*)n;
            out += "("; out += op_name(u->op); out += " ";
            dump_node(out, u->e, 0); out += ")";
            break;
        }
        case N_ASSIGN: {
            AssignNode* a = (AssignNode*)n;
            out += "("; out += op_name(a->op); out += " ";
            dump_node(out, a->target, 0); out += " ";
            dump_node(out, a->value, 0); out += ")";
            break;
        }
        case N_CALL: {
            CallNode* c = (CallNode*)n;
            out += "(call "; dump_node(out, c->callee, 0);
            for (int i = 0; i < c->args.size(); i++) { out += " "; dump_node(out, c->args[i], 0); }
            out += ")";
            break;
        }
        case N_INDEX: {
            IndexNode* ix = (IndexNode*)n;
            out += "(index "; dump_node(out, ix->obj, 0); out += " ";
            dump_node(out, ix->idx, 0); out += ")";
            break;
        }
        case N_CAST: {
            CastNode* c = (CastNode*)n;
            out += "(cast "; out += ty_name(c->type); out += " ";
            dump_node(out, c->e, 0); out += ")";
            break;
        }
        default: out += "(?)"; break;
    }
}

void ast_dump(String& out, Node* root) {
    dump_node(out, root, 0);
}

// ---- 节点计数与校验 ----
int ast_count_nodes(Node* root) {
    if (!root) return 0;
    int n = 1;
    switch (root->kind) {
        case N_PROGRAM: { ProgramNode* p = (ProgramNode*)root;
            for (int i = 0; i < p->decls.size(); i++) n += ast_count_nodes(p->decls[i]);
            break; }
        case N_FUNC: { FuncNode* f = (FuncNode*)root; n += ast_count_nodes((Node*)f->body); break; }
        case N_VARDECL: { VarDecl* v = (VarDecl*)root; n += ast_count_nodes(v->init); break; }
        case N_BLOCK: { Block* b = (Block*)root;
            for (int i = 0; i < b->stmts.size(); i++) n += ast_count_nodes(b->stmts[i]);
            break; }
        case N_IF: { IfStmt* s = (IfStmt*)root;
            n += ast_count_nodes(s->cond) + ast_count_nodes(s->thenb) + ast_count_nodes(s->elseb); break; }
        case N_WHILE: { WhileStmt* s = (WhileStmt*)root;
            n += ast_count_nodes(s->cond) + ast_count_nodes(s->body); break; }
        case N_FOR: { ForStmt* s = (ForStmt*)root;
            n += ast_count_nodes(s->init) + ast_count_nodes(s->cond) +
                 ast_count_nodes(s->post) + ast_count_nodes(s->body); break; }
        case N_RETURN: { ReturnStmt* s = (ReturnStmt*)root; n += ast_count_nodes(s->e); break; }
        case N_EXPRSTMT: { ExprStmt* s = (ExprStmt*)root; n += ast_count_nodes(s->e); break; }
        case N_BIN: { BinNode* b = (BinNode*)root; n += ast_count_nodes(b->l) + ast_count_nodes(b->r); break; }
        case N_UN: { UnNode* u = (UnNode*)root; n += ast_count_nodes(u->e); break; }
        case N_ASSIGN: { AssignNode* a = (AssignNode*)root;
            n += ast_count_nodes(a->target) + ast_count_nodes(a->value); break; }
        case N_CALL: { CallNode* cc = (CallNode*)root;
            n += ast_count_nodes(cc->callee);
            for (int i = 0; i < cc->args.size(); i++) n += ast_count_nodes(cc->args[i]);
            break; }
        case N_INDEX: { IndexNode* ix = (IndexNode*)root;
            n += ast_count_nodes(ix->obj) + ast_count_nodes(ix->idx); break; }
        default: break;
    }
    return n;
}

int ast_validate(Node* root) {
    if (!root) return 1;
    int errs = 0;
    switch (root->kind) {
        case N_FUNC: { FuncNode* f = (FuncNode*)root;
            if (!f->type) errs++;
            if (!f->body) errs++;      // 子集里函数都应有函数体
            break; }
        case N_BIN: { BinNode* b = (BinNode*)root;
            if (!b->l || !b->r) errs++;
            else errs += ast_validate(b->l) + ast_validate(b->r);
            break; }
        case N_IF: { IfStmt* s = (IfStmt*)root;
            if (!s->cond || !s->thenb) errs++;
            break; }
        default: break;
    }
    return errs;
}
// ---- 自测试 ----
int ast_self_test() {
    int fails = 0;
    CompContext cc;

    // 手工搭一棵小 AST：
    //   int f(int a){ int b = a + 2; return b * 3; }
    Type* t_int = cc.make_type(TY_INT);
    Type* t_fn = cc.make_type(TY_FUNC, t_int, 1);
    FuncNode* f = make_func(cc, t_fn, "f", 1);
    f->param_names.push(cc.dup("a", 1));
    Block* body = make_block(cc);
    VarNode* a = make_var(cc, "a", 1);
    BinNode* add = make_bin(cc, OP_ADD, a, make_litint(cc, 2));
    VarDecl* b = make_vardecl(cc, t_int, "b", 1, add);
    body->stmts.push(b);
    VarNode* bref = make_var(cc, "b", 1);
    BinNode* mul = make_bin(cc, OP_MUL, bref, make_litint(cc, 3));
    body->stmts.push(make_return(cc, mul));
    f->body = body;
    ProgramNode* prog = make_program(cc);
    prog->decls.push(f);

    String out;
    ast_dump(out, prog);

    // 验证关键结构
    if (prog->decls.size() != 1) fails++;
    if (f->body->stmts.size() != 2) fails++;
    if (mul->op != OP_MUL || mul->r->kind != N_LITINT) fails++;
    if (add->op != OP_ADD) fails++;

    // dump 文本应包含函数名与表达式
    if (out.find("f") < 0) fails++;
    if (out.find("return") < 0) fails++;

    // arena 扩容路径：分配远超初始 64KB 的节点
    for (int i = 0; i < 20000; i++) {
        LitIntNode* x = make_litint(cc, i);
        if (!x) { fails++; break; }
    }

    // 错误计数 API
    cc.error_at(5, "test error");
    if (cc.err_count != 1) fails++;

    // 节点计数与校验
    int cnt = ast_count_nodes(prog);
    if (cnt < 5) fails++;                 // 手工树至少应有若干节点
    if (ast_validate(prog) != 0) fails++;
    // 空树计数为 0
    if (ast_count_nodes(0) != 0) fails++;

    return fails;
}

} // namespace compiler
} // namespace nefu
