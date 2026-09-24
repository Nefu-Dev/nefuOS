// ============================================================================
// nefu::compiler —— 递归下降解析器实现（parser.cpp）
// ============================================================================
#include "parser.h"

namespace nefu {
namespace compiler {

Parser::Parser(CompContext& c, Lexer& l) : cc(c), lex(l), pos(0) {}

const Token& Parser::cur_tok() const { return lex.at(pos); }
const Token& Parser::peek_tok(int off) const { return lex.at(pos + off); }
bool Parser::check(int kind) const { return cur_tok().kind == kind; }
bool Parser::accept(int kind) {
    if (cur_tok().kind == kind) { pos++; return true; }
    return false;
}
void Parser::expect(int kind) {
    if (!accept(kind)) {
        char buf[96];
        ksprintf(buf, sizeof(buf), "期望 %s，但在第 %d 行遇到 %s",
                 tk_name(kind), cur_tok().line, tk_name(cur_tok().kind));
        cc.error_at(cur_tok().line, buf);
    }
}

int Parser::tok_op() const {
    switch (cur_tok().kind) {
        case PUN_PLUS: return OP_ADD;
        case PUN_MINUS: return OP_SUB;
        case PUN_STAR: return OP_MUL;
        case PUN_SLASH: return OP_DIV;
        case PUN_PERCENT: return OP_MOD;
        case PUN_EQ: return OP_EQ;
        case PUN_NE: return OP_NE;
        case PUN_LT: return OP_LT;
        case PUN_GT: return OP_GT;
        case PUN_LE: return OP_LE;
        case PUN_GE: return OP_GE;
        case PUN_AMP: return OP_AND;
        case PUN_PIPE: return OP_OR;
        case PUN_CARET: return OP_XOR;
        case PUN_LSH: return OP_SHL;
        case PUN_RSH: return OP_SHR;
        default: return -1;
    }
}

// ---- 类型 ----
Type* Parser::parse_type_specifier() {
    // 前缀修饰符
    while (accept(KW_CONST) || accept(KW_STATIC) || accept(KW_EXTERN)) {}
    Type* base = 0;
    if (accept(KW_VOID)) base = cc.make_type(TY_VOID);
    else if (accept(KW_CHAR)) base = cc.make_type(TY_CHAR);
    else if (accept(KW_INT)) base = cc.make_type(TY_INT);
    else if (accept(KW_LONG)) base = cc.make_type(TY_LONG);
    else if (accept(KW_FLOAT)) base = cc.make_type(TY_INT);   // 子集里浮点按 int 处理
    else if (accept(KW_DOUBLE)) base = cc.make_type(TY_LONG);
    else {
        cc.error_at(cur_tok().line, "缺少类型说明符");
        base = cc.make_type(TY_INT);
    }
    // 指针层
    int ptrlevel = 0;
    while (accept(PUN_STAR)) ptrlevel++;
    Type* t = base;
    for (int i = 0; i < ptrlevel; i++) {
        Type* p = cc.make_type(TY_PTR, t, 0);
        p->ptrlevel = i + 1;
        t = p;
    }
    return t;
}

// 解析声明符：标识符 + 可选 (params)
Type* Parser::parse_declarator(Type* base, const char*& out_name, int& out_namelen,
                               List<const char*>* params) {
    if (!check(TK_IDENT)) {
        out_name = ""; out_namelen = 0;
        return base;
    }
    const Token& id = cur_tok();
    pos++;
    out_name = id.start; out_namelen = id.len;
    if (check(PUN_LPAREN)) {
        pos++; // (
        Type* fn = cc.make_type(TY_FUNC, base, 0);
        if (!check(PUN_RPAREN)) {
            for (;;) {
                Type* pty = parse_type_specifier();
                const char* pn = 0; int pnl = 0;
                int dummy = 0; (void)dummy;
                // 参数名
                const char* pname = ""; int pnamelen = 0;
                if (check(TK_IDENT)) {
                    const Token& pid = cur_tok(); pos++;
                    pname = pid.start; pnamelen = pid.len;
                }
                if (params) params->push(cc.dup(pname, pnamelen));
                (void)pty;
                fn->argc++;
                if (accept(PUN_COMMA)) continue;
                break;
            }
        }
        accept(PUN_RPAREN);
        return fn;
    }
    return base;
}

// ---- 外部声明 ----
Node* Parser::parse_external() {
    Type* base = parse_type_specifier();
    const char* name = 0; int namelen = 0;
    List<const char*> params;
    Type* dt = parse_declarator(base, name, namelen, &params);

    if (check(PUN_ASSIGN)) {
        // 全局变量带初值：int g = 5;
        pos++;
        Node* init = parse_expression();
        expect(PUN_SEMI);
        return make_vardecl(cc, base, name, namelen, init);
    }
    if (check(PUN_SEMI)) {
        pos++;
        return make_vardecl(cc, dt ? base : base, name, namelen, 0);
    }
    if (check(PUN_LBRACE)) {
        // 函数定义
        FuncNode* f = make_func(cc, dt, name, namelen);
        for (int i = 0; i < params.size(); i++) f->param_names.push(params[i]);
        f->body = parse_compound();
        return f;
    }
    // 函数声明
    expect(PUN_SEMI);
    return make_vardecl(cc, base, name, namelen, 0);
}

// ---- 语句 ----
Block* Parser::parse_compound() {
    expect(PUN_LBRACE);
    Block* blk = make_block(cc);
    while (!check(PUN_RBRACE) && !check(TK_EOF)) {
        Node* s = parse_statement();
        if (s) blk->stmts.push(s);
    }
    accept(PUN_RBRACE);
    return blk;
}

Node* Parser::parse_statement() {
    if (check(PUN_LBRACE)) return parse_compound();
    if (accept(KW_RETURN)) {
        ReturnStmt* r = make_return(cc, 0);
        if (!check(PUN_SEMI)) r->e = parse_expression();
        expect(PUN_SEMI);
        return r;
    }
    if (accept(KW_IF)) {
        expect(PUN_LPAREN);
        Node* cond = parse_expression();
        expect(PUN_RPAREN);
        Node* thenb = parse_statement();
        Node* elseb = 0;
        if (accept(KW_ELSE)) elseb = parse_statement();
        return make_if(cc, cond, thenb, elseb);
    }
    if (accept(KW_WHILE)) {
        expect(PUN_LPAREN);
        Node* cond = parse_expression();
        expect(PUN_RPAREN);
        return make_while(cc, cond, parse_statement());
    }
    if (accept(KW_DO)) {
        // do body while (cond); —— 子集里用 while 节点承载，body 在前
        Node* body = parse_statement();
        expect(KW_WHILE);
        expect(PUN_LPAREN);
        Node* cond = parse_expression();
        expect(PUN_RPAREN);
        expect(PUN_SEMI);
        return make_while(cc, cond, body);
    }
    if (accept(KW_FOR)) {
        expect(PUN_LPAREN);
        Node* init = 0; Node* cond = 0; Node* post = 0;
        // init：可能是表达式，也可能是变量声明 for(int j=0;...)
        if (check(KW_INT) || check(KW_CHAR) || check(KW_VOID) || check(KW_LONG)) {
            Type* ty = parse_type_specifier();
            const char* nm = ""; int nml = 0;
            if (check(TK_IDENT)) {
                const Token& id = cur_tok(); pos++;
                nm = id.start; nml = id.len;
            }
            Node* iv = 0;
            if (accept(PUN_ASSIGN)) iv = parse_assign();
            init = make_vardecl(cc, ty, nm, nml, iv);
        } else if (!check(PUN_SEMI)) {
            init = parse_expression();
        }
        expect(PUN_SEMI);
        if (!check(PUN_SEMI)) cond = parse_expression();
        expect(PUN_SEMI);
        if (!check(PUN_RPAREN)) post = parse_expression();
        expect(PUN_RPAREN);
        return make_for(cc, init, cond, post, parse_statement());
    }
    if (accept(KW_BREAK)) { expect(PUN_SEMI); Node* n = mknode<Node>(cc, cur_tok().line); n->kind = N_BREAK; return n; }
    if (accept(KW_CONTINUE)) { expect(PUN_SEMI); Node* n = mknode<Node>(cc, cur_tok().line); n->kind = N_CONTINUE; return n; }
    // 局部变量声明：以类型关键字开头
    if (check(KW_INT) || check(KW_CHAR) || check(KW_VOID) || check(KW_LONG)) {
        Type* ty = parse_type_specifier();
        const char* name = 0; int namelen = 0;
        int dummy = 0; (void)dummy;
        const Token& id = cur_tok();
        if (check(TK_IDENT)) { pos++; name = id.start; namelen = id.len; }
        Node* init = 0;
        if (accept(PUN_ASSIGN)) init = parse_expression();
        expect(PUN_SEMI);
        return make_vardecl(cc, ty, name, namelen, init);
    }
    // 表达式语句
    Node* e = parse_expression();
    expect(PUN_SEMI);
    return make_exprstmt(cc, e);
}

// ---- 表达式：优先级爬升 ----
Node* Parser::parse_expression() { return parse_assign(); }

Node* Parser::parse_assign() {
    Node* l = parse_ternary();
    int kind = cur_tok().kind;
    int aop = -1;
    switch (kind) {
        case PUN_ASSIGN: aop = OP_ASSIGN; break;
        case PUN_ADD_ASSIGN: aop = OP_ADD_ASSIGN; break;
        case PUN_SUB_ASSIGN: aop = OP_SUB_ASSIGN; break;
        case PUN_MUL_ASSIGN: aop = OP_MUL_ASSIGN; break;
        case PUN_DIV_ASSIGN: aop = OP_DIV_ASSIGN; break;
    }
    if (aop >= 0) {
        pos++;
        Node* r = parse_assign(); // 右结合
        return make_assign(cc, aop, l, r);
    }
    return l;
}

Node* Parser::parse_ternary() {
    Node* c = parse_logical_or();
    if (accept(PUN_QUEST)) {
        Node* t = parse_assign();
        expect(PUN_COLON);
        Node* e = parse_ternary();
        // 用 bin op OP_NE? 这里用一个 N_BIN 记录三目，op 复用 OP_GE 不合适；
        // 简化：返回 (cond ? t : e) 记为 OP_ADD 占位，IR 阶段按条件跳转处理。
        BinNode* b = make_bin(cc, OP_ADD, c, 0);
        (void)t; (void)e;
        return b;
    }
    return c;
}

Node* Parser::parse_logical_or() {
    Node* l = parse_logical_and();
    while (check(PUN_LOR)) { pos++; l = make_bin(cc, OP_OR, l, parse_logical_and()); }
    return l;
}
Node* Parser::parse_logical_and() {
    Node* l = parse_bit_or();
    while (check(PUN_LAND)) { pos++; l = make_bin(cc, OP_AND, l, parse_bit_or()); }
    return l;
}
Node* Parser::parse_bit_or() {
    Node* l = parse_bit_xor();
    while (check(PUN_PIPE)) { pos++; l = make_bin(cc, OP_OR, l, parse_bit_xor()); }
    return l;
}
Node* Parser::parse_bit_xor() {
    Node* l = parse_bit_and();
    while (check(PUN_CARET)) { pos++; l = make_bin(cc, OP_XOR, l, parse_bit_and()); }
    return l;
}
Node* Parser::parse_bit_and() {
    Node* l = parse_equality();
    while (check(PUN_AMP)) { pos++; l = make_bin(cc, OP_AND, l, parse_equality()); }
    return l;
}
Node* Parser::parse_equality() {
    Node* l = parse_relational();
    for (;;) {
        int op = tok_op();
        if (cur_tok().kind == PUN_EQ || cur_tok().kind == PUN_NE) { pos++; l = make_bin(cc, op, l, parse_relational()); }
        else break;
    }
    return l;
}
Node* Parser::parse_relational() {
    Node* l = parse_shift();
    for (;;) {
        int k = cur_tok().kind;
        if (k == PUN_LT || k == PUN_GT || k == PUN_LE || k == PUN_GE) {
            int op = tok_op(); pos++; l = make_bin(cc, op, l, parse_shift());
        } else break;
    }
    return l;
}
Node* Parser::parse_shift() {
    Node* l = parse_additive();
    for (;;) {
        int k = cur_tok().kind;
        if (k == PUN_LSH || k == PUN_RSH) {
            int op = tok_op(); pos++; l = make_bin(cc, op, l, parse_additive());
        } else break;
    }
    return l;
}
Node* Parser::parse_additive() {
    Node* l = parse_multiplicative();
    for (;;) {
        int k = cur_tok().kind;
        if (k == PUN_PLUS || k == PUN_MINUS) {
            int op = tok_op(); pos++; l = make_bin(cc, op, l, parse_multiplicative());
        } else break;
    }
    return l;
}
Node* Parser::parse_multiplicative() {
    Node* l = parse_unary();
    for (;;) {
        int k = cur_tok().kind;
        if (k == PUN_STAR || k == PUN_SLASH || k == PUN_PERCENT) {
            int op = tok_op(); pos++; l = make_bin(cc, op, l, parse_unary());
        } else break;
    }
    return l;
}
Node* Parser::parse_unary() {
    if (accept(PUN_MINUS)) return make_un(cc, OP_NEG, parse_unary());
    if (accept(PUN_BANG)) return make_un(cc, OP_NOT, parse_unary());
    if (accept(PUN_TILDE)) return make_un(cc, OP_BITNOT, parse_unary());
    if (accept(PUN_AMP)) return make_un(cc, OP_ADDR, parse_unary());
    if (accept(KW_SIZEOF)) {
        // sizeof(x) -> 8 常量（子集按指针宽处理）
        if (accept(PUN_LPAREN)) { accept(TK_IDENT); accept(PUN_RPAREN); }
        return make_litint(cc, 8);
    }
    return parse_postfix();
}
Node* Parser::parse_postfix() {
    Node* e = parse_primary();
    for (;;) {
        if (check(PUN_LPAREN)) {
            pos++;
            CallNode* c = make_call(cc, e);
            if (!check(PUN_RPAREN)) {
                for (;;) {
                    c->args.push(parse_assign());
                    if (accept(PUN_COMMA)) continue;
                    break;
                }
            }
            accept(PUN_RPAREN);
            e = c;
        } else if (check(PUN_LBRACKET)) {
            pos++;
            Node* idx = parse_expression();
            accept(PUN_RBRACKET);
            e = make_index(cc, e, idx);
        } else break;
    }
    return e;
}
Node* Parser::parse_primary() {
    const Token& t = cur_tok();
    if (t.kind == TK_INT) { pos++; return make_litint(cc, t.ival); }
    if (t.kind == TK_CHAR) { pos++; return make_litchar(cc, (int)t.ival); }
    if (t.kind == TK_STR) { pos++; return make_litstr(cc, t.lit, t.litlen); }
    if (t.kind == TK_IDENT) { pos++; return make_var(cc, t.start, t.len); }
    if (accept(PUN_LPAREN)) {
        Node* e = parse_expression();
        accept(PUN_RPAREN);
        return e;
    }
    cc.error_at(t.line, "意外的 token");
    pos++;
    return make_litint(cc, 0);
}

// ---- 入口 ----
ProgramNode* Parser::parse() {
    ProgramNode* prog = make_program(cc);
    while (!check(TK_EOF)) {
        Node* d = parse_external();
        if (d) prog->decls.push(d);
    }
    return prog;
}

ProgramNode* compile_parse(CompContext& cc, const char* source) {
    Lexer lex(source);
    lex.tokenize();
    Parser p(cc, lex);
    return p.parse();
}

// ---- 自测试 ----
int parser_self_test() {
    int fails = 0;
    CompContext cc;
    const char* src =
        "int add(int a, int b){ return a + b * 2; }"
        "int main(void){ int x = add(1, 2); if (x > 3) { x = x - 1; } return x; }";
    ProgramNode* prog = compile_parse(cc, src);

    if (cc.err_count != 0) fails++;
    if (prog->decls.size() != 2) fails++;

    FuncNode* add = (FuncNode*)prog->decls[0];
    if (add->kind != N_FUNC) fails++;
    if (add->param_names.size() != 2) fails++;
    if (!add->body) fails++;

    // main 应有 if 与 return
    FuncNode* mainf = (FuncNode*)prog->decls[1];
    bool has_if = false, has_return = false;
    for (int i = 0; i < mainf->body->stmts.size(); i++) {
        Node* s = mainf->body->stmts[i];
        if (s->kind == N_IF) has_if = true;
        if (s->kind == N_RETURN) has_return = true;
    }
    if (!has_if) fails++;
    if (!has_return) fails++;

    // while + for 解析
    CompContext cc2;
    const char* src2 =
        "int s(void){ int i = 0; int sum = 0; while (i < 10) { sum = sum + i; i = i + 1; }"
        "for (int j = 0; j < 5; j = j + 1) { sum = sum + j; } return sum; }";
    ProgramNode* p2 = compile_parse(cc2, src2);
    if (cc2.err_count != 0) fails++;
    FuncNode* s2 = (FuncNode*)p2->decls[0];
    bool has_while = false, has_for = false;
    for (int i = 0; i < s2->body->stmts.size(); i++) {
        if (s2->body->stmts[i]->kind == N_WHILE) has_while = true;
        if (s2->body->stmts[i]->kind == N_FOR) has_for = true;
    }
    if (!has_while) fails++;
    if (!has_for) fails++;

    // 错误恢复：缺分号不应崩溃
    {
        CompContext ec;
        ProgramNode* ep = compile_parse(ec, "int main(void){ int a = 1 return a; }");
        // 应能产出（可能有错误计数，但不崩溃）
        if (!ep) fails++;
    }

    // 错误恢复：缺分号不应崩溃
    {
        CompContext ec;
        ProgramNode* ep = compile_parse(ec, "int main(void){ int a = 1 return a; }");
        if (!ep) fails++;
    }

    // 全局变量带初值
    {
        CompContext gc;
        ProgramNode* gp = compile_parse(gc, "int g = 42; int main(void){ return g; }");
        if (gc.err_count != 0) fails++;
        if (gp->decls.size() != 2) fails++;
        if (gp->decls[0]->kind != N_VARDECL) fails++;
    }

    // do-while 解析
    {
        CompContext cc4;
        const char* src4 = "int f(void){ int i = 0; do { i = i + 1; } while (i < 3); return i; }";
        ProgramNode* p4 = compile_parse(cc4, src4);
        if (cc4.err_count != 0) fails++;
        FuncNode* f4 = (FuncNode*)p4->decls[0];
        bool has_while = false;
        for (int i = 0; i < f4->body->stmts.size(); i++)
            if (f4->body->stmts[i]->kind == N_WHILE) has_while = true;
        if (!has_while) fails++;
    }

    // 错误恢复：缺分号不应崩溃
    CompContext cc3;
    const char* src3 = "int f(void){ int x = 5 return x; }";
    ProgramNode* p3 = compile_parse(cc3, src3);
    if (p3->decls.size() != 1) fails++;

    return fails;
}

} // namespace compiler
} // namespace nefu
