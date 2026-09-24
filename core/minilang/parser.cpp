// ============================================================================
// nefu::minilang —— 递归下降解析器（实现）
// ============================================================================
#include "parser.h"

namespace nefu {
namespace minilang {

Parser::Parser(const char* source)
    : lex(source), error(false), errmsg(), prog(0) {
    advance();   // 取第一个 token
}

void Parser::advance() { cur = lex.next(); }

bool Parser::accept(TokenKind k) {
    if (cur.kind == k) { advance(); return true; }
    return false;
}

bool Parser::expect(TokenKind k, const char* what) {
    if (cur.kind == k) { advance(); return true; }
    fail(what);
    return false;
}

void Parser::fail(const char* msg) {
    if (error) return;   // 只记第一个错误
    error = true;
    errmsg = String("line ") + /* line number */ String("");
    // 简单写死行号信息（Lexer 的 cur.line）
    char buf[32];
    int n = cur.line;
    int w = 0; char tmp[16]; int p = 0;
    if (n == 0) tmp[p++] = '0';
    while (n > 0) { tmp[p++] = (char)('0' + n % 10); n /= 10; }
    while (p > 0) buf[w++] = tmp[--p];
    buf[w] = 0;
    errmsg = String("parse error at line ");
    errmsg += buf;
    errmsg += ": ";
    errmsg += msg;
}

// ---------------- 语句 ----------------
Node* Parser::parse_let() {
    // cur 是 TOK_LET
    advance(); // 吃 let
    if (cur.kind != TOK_IDENT) { fail("expected variable name after let"); return 0; }
    const char* name = ast_intern(prog, cur.text, cur.len);
    int nlen = cur.len;
    advance();
    Node* init = 0;
    if (accept(TOK_ASSIGN)) init = parse_expr();
    accept(TOK_SEMI);   // 分号可选
    LetStmt* s = new LetStmt();
    s->kind = N_LET; s->line = cur.line;
    s->name = name; s->namelen = nlen; s->init = init;
    return ast_alloc(prog, s);
}

Node* Parser::parse_if() {
    advance(); // 吃 if
    expect(TOK_LPAREN, "expected ( after if");
    Node* cond = parse_expr();
    expect(TOK_RPAREN, "expected ) after if condition");
    Node* thenb = parse_stmt();
    Node* elseb = 0;
    if (accept(TOK_ELSE)) elseb = parse_stmt();
    IfStmt* s = new IfStmt();
    s->kind = N_IF; s->line = cur.line;
    s->cond = cond; s->thenb = thenb; s->elseb = elseb;
    return ast_alloc(prog, s);
}

Node* Parser::parse_while() {
    advance();
    expect(TOK_LPAREN, "expected ( after while");
    Node* cond = parse_expr();
    expect(TOK_RPAREN, "expected ) after while condition");
    Node* body = parse_stmt();
    WhileStmt* s = new WhileStmt();
    s->kind = N_WHILE; s->line = cur.line;
    s->cond = cond; s->body = body;
    return ast_alloc(prog, s);
}

Node* Parser::parse_for() {
    advance();
    expect(TOK_LPAREN, "expected ( after for");
    Node* init = 0;
    if (accept(TOK_SEMI)) {
        init = 0;
    } else if (cur.kind == TOK_LET) {
        init = parse_let();
    } else {
        init = parse_exprstmt();
    }
    Node* cond = 0;
    if (cur.kind != TOK_SEMI) cond = parse_expr();
    accept(TOK_SEMI);
    Node* post = 0;
    if (cur.kind != TOK_RPAREN) post = parse_expr();
    expect(TOK_RPAREN, "expected ) after for clauses");
    Node* body = parse_stmt();
    ForStmt* s = new ForStmt();
    s->kind = N_FOR; s->line = cur.line;
    s->init = init; s->cond = cond; s->post = post; s->body = body;
    return ast_alloc(prog, s);
}

Node* Parser::parse_return() {
    advance();
    Node* e = 0;
    if (cur.kind != TOK_SEMI && cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) {
        e = parse_expr();
    }
    accept(TOK_SEMI);
    ReturnStmt* s = new ReturnStmt();
    s->kind = N_RETURN; s->line = cur.line; s->e = e;
    return ast_alloc(prog, s);
}

Node* Parser::parse_block() {
    expect(TOK_LBRACE, "expected {");
    BlockStmt* b = new BlockStmt();
    b->kind = N_BLOCK; b->line = cur.line;
    while (cur.kind != TOK_RBRACE && cur.kind != TOK_EOF && !error) {
        Node* s = parse_stmt();
        if (s) b->stmts.push(s);
    }
    expect(TOK_RBRACE, "expected }");
    return ast_alloc(prog, b);
}

Node* Parser::parse_exprstmt() {
    Node* e = parse_expr();
    accept(TOK_SEMI);
    ExprStmt* s = new ExprStmt();
    s->kind = N_EXPR_STMT; s->line = cur.line; s->e = e;
    return ast_alloc(prog, s);
}

Node* Parser::parse_stmt() {
    switch (cur.kind) {
    case TOK_LET:    return parse_let();
    case TOK_IF:     return parse_if();
    case TOK_WHILE:  return parse_while();
    case TOK_FOR:    return parse_for();
    case TOK_RETURN: return parse_return();
    case TOK_LBRACE: return parse_block();
    case TOK_FN: {
        // fn name(params) { body } 作为顶层/语句
        advance();
        FuncNode* f = new FuncNode();
        f->kind = N_FUNC; f->line = cur.line;
        if (cur.kind == TOK_IDENT) {
            f->name = ast_intern(prog, cur.text, cur.len);
            f->namelen = cur.len;
            advance();
        } else { f->name = ast_intern(prog, "", 0); f->namelen = 0; }
        expect(TOK_LPAREN, "expected ( after fn name");
        while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF && !error) {
            if (cur.kind != TOK_IDENT) { fail("expected parameter name"); break; }
            f->params.push(ast_intern(prog, cur.text, cur.len));
            f->paramlen.push(cur.len);
            advance();
            if (!accept(TOK_COMMA)) break;
        }
        expect(TOK_RPAREN, "expected ) after parameters");
        f->body = parse_block();
        return ast_alloc(prog, f);
    }
    default: return parse_exprstmt();
    }
}

// ---------------- 表达式（优先级爬升） ----------------
static int bin_op_for(TokenKind k) {
    switch (k) {
    case TOK_PLUS:  return OP_ADD;
    case TOK_MINUS: return OP_SUB;
    case TOK_STAR:  return OP_MUL;
    case TOK_SLASH: return OP_DIV;
    case TOK_PERCENT: return OP_MOD;
    case TOK_EQ:    return OP_EQ;
    case TOK_NEQ:   return OP_NEQ;
    case TOK_LT:    return OP_LT;
    case TOK_GT:    return OP_GT;
    case TOK_LE:    return OP_LE;
    case TOK_GE:    return OP_GE;
    case TOK_AND: case TOK_AND_AND: return OP_AND;
    case TOK_OR:  case TOK_OR_OR:   return OP_OR;
    }
    return -1;
}

Node* Parser::parse_expr() {
    // 赋值：目标 = 值（右结合）。只处理 var / index 目标。
    Node* lhs = parse_or();
    if (cur.kind == TOK_ASSIGN) {
        advance();
        Node* rhs = parse_expr();
        AssignNode* a = new AssignNode();
        a->kind = N_ASSIGN; a->line = cur.line;
        a->target = lhs; a->value = rhs;
        return ast_alloc(prog, a);
    }
    return lhs;
}

Node* Parser::parse_or() {
    Node* l = parse_and();
    while (cur.kind == TOK_OR || cur.kind == TOK_OR_OR) {
        advance();
        Node* r = parse_and();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = OP_OR; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_and() {
    Node* l = parse_eq();
    while (cur.kind == TOK_AND || cur.kind == TOK_AND_AND) {
        advance();
        Node* r = parse_eq();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = OP_AND; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_eq() {
    Node* l = parse_cmp();
    while (cur.kind == TOK_EQ || cur.kind == TOK_NEQ) {
        int op = (cur.kind == TOK_EQ) ? OP_EQ : OP_NEQ;
        advance();
        Node* r = parse_cmp();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = op; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_cmp() {
    Node* l = parse_add();
    while (cur.kind == TOK_LT || cur.kind == TOK_GT ||
           cur.kind == TOK_LE || cur.kind == TOK_GE) {
        int op = bin_op_for(cur.kind);
        advance();
        Node* r = parse_add();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = op; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_add() {
    Node* l = parse_mul();
    while (cur.kind == TOK_PLUS || cur.kind == TOK_MINUS) {
        int op = (cur.kind == TOK_PLUS) ? OP_ADD : OP_SUB;
        advance();
        Node* r = parse_mul();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = op; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_mul() {
    Node* l = parse_unary();
    while (cur.kind == TOK_STAR || cur.kind == TOK_SLASH || cur.kind == TOK_PERCENT) {
        int op = bin_op_for(cur.kind);
        advance();
        Node* r = parse_unary();
        BinNode* b = new BinNode();
        b->kind = N_BIN; b->op = op; b->l = l; b->r = r; b->line = cur.line;
        l = ast_alloc(prog, b);
    }
    return l;
}

Node* Parser::parse_unary() {
    if (cur.kind == TOK_BANG || cur.kind == TOK_NOT || cur.kind == TOK_MINUS) {
        int op = (cur.kind == TOK_MINUS) ? OP_NEG : OP_NOT;
        advance();
        Node* e = parse_unary();
        UnaryNode* u = new UnaryNode();
        u->kind = N_UNARY; u->op = op; u->e = e; u->line = cur.line;
        return ast_alloc(prog, u);
    }
    return parse_postfix();
}

Node* Parser::parse_postfix() {
    Node* e = parse_primary();
    while (true) {
        if (cur.kind == TOK_LPAREN) {
            advance();
            CallNode* c = new CallNode();
            c->kind = N_CALL; c->callee = e; c->line = cur.line;
            while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF && !error) {
                c->args.push(parse_expr());
                if (!accept(TOK_COMMA)) break;
            }
            expect(TOK_RPAREN, "expected ) after call arguments");
            e = ast_alloc(prog, c);
        } else if (cur.kind == TOK_LBRACKET) {
            advance();
            IndexNode* ix = new IndexNode();
            ix->kind = N_INDEX; ix->obj = e; ix->line = cur.line;
            ix->idx = parse_expr();
            expect(TOK_RBRACKET, "expected ] after index");
            e = ast_alloc(prog, ix);
        } else {
            break;
        }
    }
    return e;
}

Node* Parser::parse_primary() {
    Token t = cur;
    switch (t.kind) {
    case TOK_INT: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_INT; n->i = t.intval; n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_FLOAT: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_FLOAT; n->f = t.fxval; n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_STR: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_STR;
        n->s = ast_intern(prog, t.text, t.len);
        n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_TRUE: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_BOOL; n->i = 1; n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_FALSE: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_BOOL; n->i = 0; n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_NULL: {
        advance();
        LitNode* n = new LitNode();
        n->kind = N_LIT; n->tag = LIT_NULL; n->line = t.line;
        return ast_alloc(prog, n);
    }
    case TOK_IDENT: {
        advance();
        VarNode* v = new VarNode();
        v->kind = N_VAR;
        v->name = ast_intern(prog, t.text, t.len);
        v->namelen = t.len; v->line = t.line;
        return ast_alloc(prog, v);
    }
    case TOK_LPAREN: {
        advance();
        Node* e = parse_expr();
        expect(TOK_RPAREN, "expected )");
        return e;
    }
    case TOK_LBRACKET: {
        advance();
        ArrayNode* a = new ArrayNode();
        a->kind = N_ARRAY; a->line = t.line;
        while (cur.kind != TOK_RBRACKET && cur.kind != TOK_EOF && !error) {
            a->elems.push(parse_expr());
            if (!accept(TOK_COMMA)) break;
        }
        expect(TOK_RBRACKET, "expected ] after array");
        return ast_alloc(prog, a);
    }
    default:
        fail("unexpected token in expression");
        return 0;
    }
}

bool Parser::parse_program(ProgramObj* p) {
    prog = p;
    error = false;
    errmsg.clear();
    while (cur.kind != TOK_EOF && !error) {
        Node* s = parse_stmt();
        if (s) p->stmts.push(s);
    }
    return !error;
}

// 便捷入口
Value minilang_parse(const char* source, String* errmsg_out) {
    Value progv = val_make_program();
    ProgramObj* prog = val_as_program(progv);
    Parser p(source);
    bool ok = p.parse_program(prog);
    if (!ok) {
        if (errmsg_out) *errmsg_out = p.errmsg;
        val_drop(progv);
        return val_make_null();
    }
    return progv;
}

// 自测试：解析若干片段，断言不报错、节点类型正确。
int parser_self_test() {
    int fails = 0;

    // 1) 算术优先级
    {
        String err;
        Value p = minilang_parse("1 + 2 * 3", &err);
        if (val_is_null(p)) { fails++; }
        else {
            ProgramObj* pr = val_as_program(p);
            if (pr->stmts.size() != 1) fails++;
            Node* s = pr->stmts[0];
            if (s->kind != N_EXPR_STMT) fails++;
            Node* e = ((ExprStmt*)s)->e;
            // 1 + (2*3) -> 顶层应为 BIN ADD
            if (e->kind != N_BIN || ((BinNode*)e)->op != OP_ADD) fails++;
            val_drop(p);
        }
    }

    // 2) 函数定义 + 调用
    {
        String err;
        Value p = minilang_parse("fn add(a,b){ return a+b; } let r = add(2,3);", &err);
        if (val_is_null(p)) fails++;
        else {
            ProgramObj* pr = val_as_program(p);
            if (pr->stmts.size() != 2) fails++;
            if (pr->stmts[0]->kind != N_FUNC) fails++;
            FuncNode* f = (FuncNode*)pr->stmts[0];
            if (f->params.size() != 2) fails++;
            val_drop(p);
        }
    }

    // 3) 数组与下标
    {
        String err;
        Value p = minilang_parse("let a = [1,2,3]; let b = a[1];", &err);
        if (val_is_null(p)) fails++;
        else { val_drop(p); }
    }

    // 4) if/while/for
    {
        String err;
        Value p = minilang_parse("if (1) { let x=1; } else { let x=2; } while(0){} for(let i=0;i<3;i=i+1){}", &err);
        if (val_is_null(p)) fails++;
        else { val_drop(p); }
    }

    // 5) 字符串
    {
        String err;
        Value p = minilang_parse("let s = \"hello\\nworld\";", &err);
        if (val_is_null(p)) fails++;
        else { val_drop(p); }
    }

    return fails;
}

} // namespace minilang
} // namespace nefu

