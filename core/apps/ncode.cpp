// =============================================================================
//  ncode.cpp — nefuOS ".ncode" scripting language (nefu) interpreter
// -----------------------------------------------------------------------------
//  Language snapshot (".ncode" / nefu):
//    #nefu                     optional first-line marker
//    import<std>               import the standard library (console/string/os)
//    import<sysapi>            import the system API library (sysapi.h, all in)
//    func name(a, b) { ... }   function definition
//    main() { ... }            entry point
//    var x = 1;  let y = "s";  variables (int/float/str/bool/null/array)
//    if / else / while / for / break / continue / return
//    a[i], s.len, s.substr(i,n), arr.push(v), arr.pop()
//    start_r_os() -> os handle (windowed empty desktop, sandboxed user files,
//                     autosave every 0.5 s)
//
//  Constraints: no STL / no exceptions / no RTTI; kalloc/kfree + nefu::String/
//  nefu::List. Compiles on both the Win32 host and the bare-metal kernel.
//  NCODE_HOST_TEST (host-side unit harness): GUI/sysapi parts become stubs so
//  the interpreter core can be tested without booting the OS.
// =============================================================================
#include "ncode.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include "../platform.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../apps/apps.h"
#include "../sys/sysapi.h"
#include <string.h>

namespace nefu {

// =============================================================================
//  values
// =============================================================================
enum NTag { N_NULL = 0, N_BOOL, N_INT, N_FLOAT, N_STR, N_ARR, N_FN, N_OS };

struct NVal {
    int tag;
    bool b;
    int64_t i;
    double f;
    String s;           // N_STR
    List<NVal> arr;     // N_ARR
    String fn;          // N_FN (script function name)
    void* os;           // N_OS (NOS*)

    NVal() : tag(N_NULL), b(false), i(0), f(0.0), os(0) {}
};

static NVal nv_null()                { NVal v; return v; }
static NVal nv_bool(bool b)          { NVal v; v.tag = N_BOOL; v.b = b; return v; }
static NVal nv_int(int64_t i)        { NVal v; v.tag = N_INT; v.i = i; return v; }
static NVal nv_float(double f)       { NVal v; v.tag = N_FLOAT; v.f = f; return v; }
static NVal nv_str(const char* s)    { NVal v; v.tag = N_STR; v.s = s; return v; }
static NVal nv_str(const String& s)  { NVal v; v.tag = N_STR; v.s = s; return v; }
static NVal nv_arr()                 { NVal v; v.tag = N_ARR; return v; }
static NVal nv_fn(const char* n)     { NVal v; v.tag = N_FN; v.fn = n; return v; }
static NVal nv_os(void* p)           { NVal v; v.tag = N_OS; v.os = p; return v; }

static bool nv_is_num(const NVal& v) { return v.tag == N_INT || v.tag == N_FLOAT; }
static double nv_as_num(const NVal& v) { return v.tag == N_FLOAT ? v.f : (double)v.i; }
static int64_t nv_as_int(const NVal& v) { return v.tag == N_FLOAT ? (int64_t)v.f : v.i; }
static bool nv_truthy(const NVal& v) {
    switch (v.tag) {
        case N_NULL:  return false;
        case N_BOOL:  return v.b;
        case N_INT:   return v.i != 0;
        case N_FLOAT: return v.f != 0.0;
        case N_STR:   return v.s.len() > 0;
        case N_ARR:   return v.arr.size() > 0;
        default:      return true;
    }
}

// =============================================================================
//  AST (single node struct; kind tag in k)
// =============================================================================
enum NK {
    K_PROG = 0,
    K_LET, K_EXPR, K_IF, K_WHILE, K_FOR, K_BLOCK, K_RET, K_BRK, K_CONT, K_FUNCDEF,
    K_LIT, K_VAR, K_BIN, K_UN, K_CALL, K_ASSIGN, K_ARR, K_INDEX, K_MEMBER, K_INCDEC
};

struct NNode {
    int k;
    int line;
    NVal lit;             // K_LIT
    String name;          // var / assign target / call callee / func name
    String member;        // K_MEMBER
    int op;               // K_BIN / K_UN operator id
    NNode* a;             // primary child (bin left / un operand / if-cond / while-cond /
                          //   for-init / index-obj / member-obj / assign-value-target)
    NNode* b;             // secondary (bin right / if-then / while-body / for-cond / index-idx /
                          //   assign value / func body)
    NNode* c;             // tertiary (if-else / for-post / index-assign index)
    List<NNode*> args;    // call args / array elems
    List<NNode*> stmts;   // block / func body / program / for body (stmts[0])
    List<String> params;  // func params
    bool flag;            // K_MEMBER: is_call; K_ASSIGN: index-assignment

    NNode() : k(K_PROG), line(0), op(0), a(0), b(0), c(0), flag(false) {}
};

static void node_free(NNode* n) {
    if (!n) return;
    node_free(n->a); node_free(n->b); node_free(n->c);
    for (int i = 0; i < n->args.size(); i++) node_free(n->args[i]);
    for (int i = 0; i < n->stmts.size(); i++) node_free(n->stmts[i]);
    delete n;
}

static NNode* node_new(int k, int line) {
    NNode* n = new NNode();
    n->k = k; n->line = line;
    return n;
}

// =============================================================================
//  environment
// =============================================================================
struct NBind { String name; NVal val; };
struct NEnv {
    NEnv* parent;
    List<NBind> binds;
    NEnv() : parent(0) {}
};

static NEnv* env_new(NEnv* parent) {
    NEnv* e = new NEnv();
    e->parent = parent;
    return e;
}

static NVal* env_find(NEnv* e, const char* name) {
    for (NEnv* cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->binds.size(); i++) {
            if (cur->binds[i].name == name) return &cur->binds[i].val;
        }
    }
    return 0;
}

static void env_declare(NEnv* e, const char* name, const NVal& v) {
    NBind b; b.name = name; b.val = v;
    e->binds.push(b);
}

static bool env_set(NEnv* e, const char* name, const NVal& v) {
    NVal* p = env_find(e, name);
    if (!p) return false;
    *p = v;
    return true;
}

// =============================================================================
//  runtime context
// =============================================================================
struct NOS;

struct NCtx {
    NEnv* env;
    List<NNode*> funcs;
    NCOut* out;
    List<String> args;       // extra command-line arguments (args())
    String sandbox;          // sandbox dir name for start_r_os()
    NOS* os;                 // live start_r_os() handle (0 if none)
    bool err;
    bool stop;               // process_exit() requested
    int  ctl;                // 0=normal 1=return
    NVal ret;
    bool has_sysapi, has_std;
    uint32_t rng;

    NCtx() : env(0), out(0), os(0), err(false), stop(false), ctl(0),
             has_sysapi(false), has_std(false), rng(0x12345678u) {}
};

static void ctx_print(NCtx* ctx, const char* s) { if (ctx->out) ctx->out->p(s); }
static void ctx_pln(NCtx* ctx, const char* s)   { if (ctx->out) { ctx->out->p(s); ctx->out->p("\n"); } }
static void ctx_pln(NCtx* ctx)                  { if (ctx->out) ctx->out->p("\n"); }

// =============================================================================
//  lexer
// =============================================================================
enum Tk {
    TK_EOF = 0, TK_ID, TK_NUM, TK_STR, TK_KW,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_EQ, TK_NEQ, TK_LT_ANGLE, TK_GT_ANGLE, TK_LE, TK_GE,
    TK_AND, TK_OR, TK_NOT,
    TK_ASSIGN, TK_LP, TK_RP, TK_LB, TK_RB, TK_LC, TK_RC, TK_COMMA, TK_SEMI, TK_DOT,
    TK_INC, TK_DEC
};

struct NTok {
    int tk;
    int line;
    String text;   // identifier / keyword / number raw text / string value
    int64_t i;
    double f;
    bool is_float;
};

static bool tok_is_kw(const NTok& t, const char* kw) {
    return t.tk == TK_KW && t.text == kw;
}

static bool n_is_id_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static bool n_is_digit(char c) { return c >= '0' && c <= '9'; }
static unsigned n_strtou_hex(const char* s);
static bool n_is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static const char* const NKEYWORDS[] = {
    "var", "let", "func", "if", "else", "while", "for",
    "break", "continue", "return", "true", "false", "null",
    "import", "main", 0
};

static bool n_is_keyword(const char* s) {
    for (int i = 0; NKEYWORDS[i]; i++) if (nefu::strcmp(s, NKEYWORDS[i]) == 0) return true;
    return false;
}

static bool n_lex(const char* src, int len, List<NTok>& toks, String& err) {
    int i = 0;
    int line = 1;
    while (i < len) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '\n') { i++; line++; continue; }
        // comments
        if (c == '#' || (c == '/' && i + 1 < len && src[i + 1] == '/')) {
            while (i < len && src[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < len && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') line++;
                i++;
            }
            i += 2;
            continue;
        }
        NTok t; t.tk = TK_EOF; t.line = line; t.i = 0; t.f = 0.0; t.is_float = false;
        // string literal
        if (c == '"' || c == '\'') {
            char q = c;
            i++;
            String s;
            bool closed = false;
            while (i < len) {
                char ch = src[i];
                if (ch == q) { i++; closed = true; break; }
                if (ch == '\\' && i + 1 < len) {
                    char e = src[i + 1];
                    i += 2;
                    switch (e) {
                        case 'n': s += '\n'; break;
                        case 't': s += '\t'; break;
                        case 'r': s += '\r'; break;
                        case '\\': s += '\\'; break;
                        case '"': s += '"'; break;
                        case '\'': s += '\''; break;
                        default:  s += e; break;
                    }
                } else {
                    if (ch == '\n') line++;
                    s += ch;
                    i++;
                }
            }
            if (!closed) { err = "unterminated string"; return false; }
            t.tk = TK_STR; t.text = s;
            toks.push(t);
            continue;
        }
        // number
        if (n_is_digit(c) || (c == '.' && i + 1 < len && n_is_digit(src[i + 1]))) {
            int start = i;
            bool is_float = false;
            // hex literal: 0x...
            if (c == '0' && i + 1 < len && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                i += 2;
                while (i < len && n_is_hex(src[i])) i++;
                String hraw(src + start, i - start);
                t.tk = TK_NUM;
                t.text = hraw;
                t.is_float = false;
                t.i = n_strtou_hex(hraw.c_str() + 2);
                toks.push(t);
                continue;
            }
            while (i < len && (n_is_digit(src[i]) || src[i] == '.')) {
                if (src[i] == '.') is_float = true;
                i++;
            }
            String raw(src + start, i - start);
            t.tk = TK_NUM;
            t.text = raw;
            t.is_float = is_float;
            if (is_float) {
                t.f = 0.0;
                double mul = 0.1;
                bool seen_dot = false;
                for (int k = 0; k < raw.len(); k++) {
                    char ch = raw[k];
                    if (ch == '.') { seen_dot = true; continue; }
                    if (!seen_dot) t.f = t.f * 10.0 + (double)(ch - '0');
                    else { t.f += (double)(ch - '0') * mul; mul *= 0.1; }
                }
            } else {
                int64_t v = 0;
                for (int k = 0; k < raw.len(); k++) v = v * 10 + (raw[k] - '0');
                t.i = v;
            }
            toks.push(t);
            continue;
        }
        // identifier / keyword
        if (n_is_id_char(c)) {
            int start = i;
            while (i < len && n_is_id_char(src[i])) i++;
            String w(src + start, i - start);
            t.text = w;
            t.tk = n_is_keyword(w.c_str()) ? TK_KW : TK_ID;
            toks.push(t);
            continue;
        }
        // operators
        t.tk = TK_EOF;
        switch (c) {
            case '+':
                if (i + 1 < len && src[i + 1] == '+') { t.tk = TK_INC; i++; }
                else t.tk = TK_PLUS;
                break;
            case '-':
                if (i + 1 < len && src[i + 1] == '-') { t.tk = TK_DEC; i++; }
                else t.tk = TK_MINUS;
                break;
            case '*': t.tk = TK_STAR; break;
            case '/': t.tk = TK_SLASH; break;
            case '%': t.tk = TK_PERCENT; break;
            case '(': t.tk = TK_LP; break;
            case ')': t.tk = TK_RP; break;
            case '[': t.tk = TK_LB; break;
            case ']': t.tk = TK_RB; break;
            case '{': t.tk = TK_LC; break;
            case '}': t.tk = TK_RC; break;
            case ',': t.tk = TK_COMMA; break;
            case ';': t.tk = TK_SEMI; break;
            case '.': t.tk = TK_DOT; break;
            case '<':
                if (i + 1 < len && src[i + 1] == '=') { t.tk = TK_LE; i++; }
                else t.tk = TK_LT_ANGLE;
                break;
            case '>':
                if (i + 1 < len && src[i + 1] == '=') { t.tk = TK_GE; i++; }
                else t.tk = TK_GT_ANGLE;
                break;
            case '=':
                if (i + 1 < len && src[i + 1] == '=') { t.tk = TK_EQ; i++; }
                else t.tk = TK_ASSIGN;
                break;
            case '!':
                if (i + 1 < len && src[i + 1] == '=') { t.tk = TK_NEQ; i++; }
                else t.tk = TK_NOT;
                break;
            case '&':
                if (i + 1 < len && src[i + 1] == '&') { t.tk = TK_AND; i++; }
                else { err = "unexpected '&'"; return false; }
                break;
            case '|':
                if (i + 1 < len && src[i + 1] == '|') { t.tk = TK_OR; i++; }
                else { err = "unexpected '|'"; return false; }
                break;
            default:
                err = "unexpected character";
                return false;
        }
        i++;
        toks.push(t);
    }
    NTok t; t.tk = TK_EOF; t.line = line; t.i = 0; t.f = 0.0; t.is_float = false;
    toks.push(t);
    return true;
}

// =============================================================================
//  parser
// =============================================================================
struct NParser {
    List<NTok> toks;
    int pos;
    NCtx* ctx;

    NParser(NCtx* c) : pos(0), ctx(c) {}

    const NTok& cur() { return toks[pos]; }
    const NTok& peek(int n = 1) {
        int p = pos + n;
        return (p < toks.size()) ? toks[p] : toks[toks.size() - 1];
    }
    void adv() { if (pos < toks.size() - 1) pos++; }

    bool expect(int tk, const char* what) {
        if (cur().tk == tk) { adv(); return true; }
        ctx_err("expected " + String(what));
        return false;
    }

    void ctx_err(const String& m) {
        if (!ctx->err) {
            ctx->err = true;
            char buf[64];
            ksprintf(buf, sizeof(buf), "line %d: ", cur().line);
            String msg = "ncode: ";
            msg += buf; msg += m;
            ctx_pln(ctx, msg.c_str());
        }
    }

    NNode* parse_expr()     { return parse_assign(); }
    NNode* parse_assign();
    NNode* parse_or();
    NNode* parse_and();
    NNode* parse_cmp();
    NNode* parse_add();
    NNode* parse_mul();
    NNode* parse_unary();
    NNode* parse_postfix(NNode* base);
    NNode* parse_primary();
    NNode* parse_stmt();
    NNode* parse_block();
    bool   parse_program(NNode* prog);
};

NNode* NParser::parse_assign() {
    NNode* e = parse_or();
    if (cur().tk == TK_ASSIGN) {
        int line = cur().line;
        adv();
        NNode* v = parse_assign();
        NNode* n = node_new(K_ASSIGN, line);
        if (e->k == K_VAR) {
            n->name = e->name;
            n->b = v;
            node_free(e);
        } else if (e->k == K_INDEX) {
            n->flag = true;                 // arr[i] = v
            n->a = e->a;                    // array expression
            n->c = e->b;                    // index expression
            n->b = v;
            node_free(e);
        } else {
            ctx_err("invalid assignment target");
            node_free(e); node_free(v);
        }
        return n;
    }
    return e;
}

NNode* NParser::parse_or() {
    NNode* l = parse_and();
    while (cur().tk == TK_OR) {
        int line = cur().line; adv();
        NNode* r = parse_and();
        NNode* n = node_new(K_BIN, line);
        n->op = 10; n->a = l; n->b = r;
        l = n;
    }
    return l;
}

NNode* NParser::parse_and() {
    NNode* l = parse_cmp();
    while (cur().tk == TK_AND) {
        int line = cur().line; adv();
        NNode* r = parse_cmp();
        NNode* n = node_new(K_BIN, line);
        n->op = 9; n->a = l; n->b = r;
        l = n;
    }
    return l;
}

NNode* NParser::parse_cmp() {
    NNode* l = parse_add();
    for (;;) {
        int op = -1;
        switch (cur().tk) {
            case TK_EQ: op = 1; break;
            case TK_NEQ: op = 2; break;
            case TK_LT_ANGLE: op = 3; break;
            case TK_GT_ANGLE: op = 4; break;
            case TK_LE: op = 5; break;
            case TK_GE: op = 6; break;
            default: return l;
        }
        int line = cur().line; adv();
        NNode* r = parse_add();
        NNode* n = node_new(K_BIN, line);
        n->op = op; n->a = l; n->b = r;
        l = n;
    }
}

NNode* NParser::parse_add() {
    NNode* l = parse_mul();
    for (;;) {
        int op = -1;
        if (cur().tk == TK_PLUS) op = 7;
        else if (cur().tk == TK_MINUS) op = 8;
        else return l;
        int line = cur().line; adv();
        NNode* r = parse_mul();
        NNode* n = node_new(K_BIN, line);
        n->op = op; n->a = l; n->b = r;
        l = n;
    }
}

NNode* NParser::parse_mul() {
    NNode* l = parse_unary();
    for (;;) {
        int op = -1;
        if (cur().tk == TK_STAR) op = 11;
        else if (cur().tk == TK_SLASH) op = 12;
        else if (cur().tk == TK_PERCENT) op = 13;
        else return l;
        int line = cur().line; adv();
        NNode* r = parse_unary();
        NNode* n = node_new(K_BIN, line);
        n->op = op; n->a = l; n->b = r;
        l = n;
    }
}

NNode* NParser::parse_unary() {
    if (cur().tk == TK_MINUS || cur().tk == TK_NOT) {
        int line = cur().line;
        int op = cur().tk == TK_MINUS ? 14 : 15;
        adv();
        NNode* e = parse_unary();
        NNode* n = node_new(K_UN, line);
        n->op = op; n->a = e;
        return n;
    }
    return parse_postfix(parse_primary());
}

NNode* NParser::parse_primary() {
    int line = cur().line;
    if (cur().tk == TK_NUM) {
        NNode* n = node_new(K_LIT, line);
        if (cur().is_float) n->lit = nv_float(cur().f);
        else n->lit = nv_int(cur().i);
        adv();
        return n;
    }
    if (cur().tk == TK_STR) {
        NNode* n = node_new(K_LIT, line);
        n->lit = nv_str(cur().text);
        adv();
        return n;
    }
    if (tok_is_kw(cur(), "true") || tok_is_kw(cur(), "false")) {
        NNode* n = node_new(K_LIT, line);
        n->lit = nv_bool(tok_is_kw(cur(), "true"));
        adv();
        return n;
    }
    if (tok_is_kw(cur(), "null")) {
        NNode* n = node_new(K_LIT, line);
        n->lit = nv_null();
        adv();
        return n;
    }
    if (cur().tk == TK_ID) {
        NNode* n = node_new(K_VAR, line);
        n->name = cur().text;
        adv();
        return n;
    }
    if (cur().tk == TK_LP) {
        adv();
        NNode* e = parse_expr();
        expect(TK_RP, "')'");
        return e;
    }
    if (cur().tk == TK_LB) {
        adv();
        NNode* n = node_new(K_ARR, line);
        while (cur().tk != TK_RB && cur().tk != TK_EOF) {
            n->args.push(parse_expr());
            if (cur().tk == TK_COMMA) adv();
            else break;
        }
        expect(TK_RB, "']'");
        return n;
    }
    ctx_err("unexpected token");
    return node_new(K_LIT, line);
}

NNode* NParser::parse_postfix(NNode* base) {
    NNode* n = base;
    for (;;) {
        if (cur().tk == TK_INC || cur().tk == TK_DEC) {   // postfix ++ / --
            if (n->k != K_VAR) { ctx_err("can only increment a variable"); node_free(n); return n; }
            NNode* c = node_new(K_INCDEC, cur().line);
            c->name = n->name;
            c->op = (cur().tk == TK_INC) ? 1 : -1;
            adv();
            node_free(n);
            n = c;
            continue;
        }
        if (cur().tk == TK_LP) {          // call: name(args)
            int line = cur().line;
            adv();
            NNode* c = node_new(K_CALL, line);
            if (n->k == K_VAR) c->name = n->name;
            else { ctx_err("can only call a function name"); node_free(n); return c; }
            while (cur().tk != TK_RP && cur().tk != TK_EOF) {
                c->args.push(parse_expr());
                if (cur().tk == TK_COMMA) adv();
                else break;
            }
            expect(TK_RP, "')'");
            node_free(n);
            n = c;
            continue;
        }
        if (cur().tk == TK_LB) {          // index: a[i]
            int line = cur().line;
            adv();
            NNode* idx = parse_expr();
            expect(TK_RB, "']'");
            NNode* c = node_new(K_INDEX, line);
            c->a = n; c->b = idx;
            n = c;
            continue;
        }
        if (cur().tk == TK_DOT) {         // member: obj.prop / obj.method(args)
            int line = cur().line;
            adv();
            if (cur().tk != TK_ID && cur().tk != TK_KW) {
                ctx_err("expected member name after '.'");
                node_free(n);
                return n;
            }
            NNode* c = node_new(K_MEMBER, line);
            c->a = n;
            c->member = cur().text;
            adv();
            if (cur().tk == TK_LP) {
                adv();
                c->flag = true;           // method call
                while (cur().tk != TK_RP && cur().tk != TK_EOF) {
                    c->args.push(parse_expr());
                    if (cur().tk == TK_COMMA) adv();
                    else break;
                }
                expect(TK_RP, "')'");
            }
            n = c;
            continue;
        }
        break;
    }
    return n;
}

NNode* NParser::parse_block() {
    int line = cur().line;
    expect(TK_LC, "'{'");
    NNode* b = node_new(K_BLOCK, line);
    while (cur().tk != TK_RC && cur().tk != TK_EOF) {
        b->stmts.push(parse_stmt());
    }
    expect(TK_RC, "'}'");
    return b;
}

NNode* NParser::parse_stmt() {
    int line = cur().line;
    if (tok_is_kw(cur(), "var") || tok_is_kw(cur(), "let")) {
        adv();
        if (cur().tk != TK_ID) { ctx_err("expected variable name"); return node_new(K_EXPR, line); }
        NNode* n = node_new(K_LET, line);
        n->name = cur().text;
        adv();
        if (cur().tk == TK_ASSIGN) {
            adv();
            n->b = parse_expr();
        }
        if (cur().tk == TK_SEMI) adv();
        return n;
    }
    if (tok_is_kw(cur(), "if")) {
        adv();
        expect(TK_LP, "'(' after if");
        NNode* n = node_new(K_IF, line);
        n->a = parse_expr();
        expect(TK_RP, "')'");
        n->b = parse_stmt();
        if (tok_is_kw(cur(), "else")) {
            adv();
            n->c = parse_stmt();
            n->flag = true;
        }
        return n;
    }
    if (tok_is_kw(cur(), "while")) {
        adv();
        expect(TK_LP, "'(' after while");
        NNode* n = node_new(K_WHILE, line);
        n->a = parse_expr();
        expect(TK_RP, "')'");
        n->b = parse_stmt();
        return n;
    }
    if (tok_is_kw(cur(), "for")) {
        adv();
        expect(TK_LP, "'(' after for");
        NNode* n = node_new(K_FOR, line);
        // init (must not eat the ';' separator)
        if (cur().tk != TK_SEMI) {
            if (tok_is_kw(cur(), "var") || tok_is_kw(cur(), "let")) {
                NNode* let = node_new(K_LET, cur().line);
                adv();
                if (cur().tk != TK_ID) { ctx_err("expected variable name"); return n; }
                let->name = cur().text;
                adv();
                if (cur().tk == TK_ASSIGN) { adv(); let->b = parse_expr(); }
                n->a = let;
            } else {
                NNode* ex = node_new(K_EXPR, cur().line);
                ex->a = parse_expr();
                n->a = ex;
            }
            // consume the first ';' (init <-> cond separator)
            if (cur().tk == TK_SEMI) adv();
        } else adv();
        // cond
        if (cur().tk != TK_SEMI) n->b = parse_expr();
        if (cur().tk == TK_SEMI) adv();
        // post
        if (cur().tk != TK_RP) n->c = parse_expr();
        expect(TK_RP, "')'");
        n->stmts.push(parse_stmt());   // body (stmts[0])
        return n;
    }
    if (tok_is_kw(cur(), "break")) {
        adv();
        NNode* n = node_new(K_BRK, line);
        if (cur().tk == TK_SEMI) adv();
        return n;
    }
    if (tok_is_kw(cur(), "continue")) {
        adv();
        NNode* n = node_new(K_CONT, line);
        if (cur().tk == TK_SEMI) adv();
        return n;
    }
    if (tok_is_kw(cur(), "return")) {
        adv();
        NNode* n = node_new(K_RET, line);
        if (cur().tk != TK_SEMI && cur().tk != TK_RC && cur().tk != TK_EOF) {
            n->a = parse_expr();
        }
        if (cur().tk == TK_SEMI) adv();
        return n;
    }
    if (cur().tk == TK_LC) return parse_block();
    NNode* n = node_new(K_EXPR, line);
    n->a = parse_expr();
    if (cur().tk == TK_SEMI) adv();
    return n;
}

bool NParser::parse_program(NNode* prog) {
    prog->k = K_PROG;
    while (cur().tk != TK_EOF) {
        if (tok_is_kw(cur(), "import")) {
            adv();
            if (cur().tk != TK_LT_ANGLE) { ctx_err("expected '<' after import"); return false; }
            adv();
            if (cur().tk != TK_ID) { ctx_err("expected library name"); return false; }
            String lib = cur().text;
            adv();
            if (cur().tk != TK_GT_ANGLE) { ctx_err("expected '>'"); return false; }
            adv();
            if (cur().tk == TK_SEMI) adv();
            if (lib == "sysapi") ctx->has_sysapi = true;
            else if (lib == "std") ctx->has_std = true;
            else { ctx_err("unknown library '" + lib + "' (use <std> or <sysapi>)"); return false; }
            continue;
        }
        if (tok_is_kw(cur(), "func")) {
            adv();
            if (cur().tk != TK_ID) { ctx_err("expected function name"); return false; }
            NNode* fn = node_new(K_FUNCDEF, cur().line);
            fn->name = cur().text;
            adv();
            expect(TK_LP, "'(' after function name");
            while (cur().tk != TK_RP && cur().tk != TK_EOF) {
                if (cur().tk != TK_ID) { ctx_err("expected parameter name"); return false; }
                fn->params.push(cur().text);
                adv();
                if (cur().tk == TK_COMMA) adv();
                else break;
            }
            expect(TK_RP, "')'");
            fn->b = parse_block();
            ctx->funcs.push(fn);
            continue;
        }
        if (tok_is_kw(cur(), "main")) {
            // main() { ... } -- the entry point; parsed as a function named "main"
            adv();
            expect(TK_LP, "'(' after main");
            while (cur().tk != TK_RP && cur().tk != TK_EOF) {
                if (cur().tk != TK_ID) { ctx_err("expected parameter name"); return false; }
                adv();
                if (cur().tk == TK_COMMA) adv();
                else break;
            }
            expect(TK_RP, "')' after main");
            NNode* fn = node_new(K_FUNCDEF, cur().line);
            fn->name = "main";
            fn->b = parse_block();
            ctx->funcs.push(fn);
            continue;
        }
        NNode* st = parse_stmt();
        if (ctx->err) { node_free(st); return false; }
        prog->stmts.push(st);
    }
    return !ctx->err;
}

// =============================================================================
//  forward declarations
// =============================================================================
static NVal n_eval_expr(NCtx* ctx, NEnv* env, NNode* e);
static int  n_eval_stmt(NCtx* ctx, NEnv* env, NNode* s);
static NVal n_call_func(NCtx* ctx, NEnv* caller, const char* name, int argc, NVal* argv);

// =============================================================================
//  helpers
// =============================================================================
static void nval_to_str(const NVal& v, String& out) {
    switch (v.tag) {
        case N_NULL:  out += "null"; break;
        case N_BOOL:  out += v.b ? "true" : "false"; break;
        case N_INT: {
            char buf[32];
            ksprintf(buf, sizeof(buf), "%lld", (long long)v.i);
            out += buf;
            break;
        }
        case N_FLOAT: {
            char buf[32];
            long long iv = (long long)v.f;
            if ((double)iv == v.f) ksprintf(buf, sizeof(buf), "%lld", iv);
            else ksprintf(buf, sizeof(buf), "%.3f", (double)v.f);
            out += buf;
            break;
        }
        case N_STR:   out += v.s; break;
        case N_ARR: {
            out += "[";
            for (int i = 0; i < v.arr.size(); i++) {
                if (i > 0) out += ", ";
                nval_to_str(v.arr[i], out);
            }
            out += "]";
            break;
        }
        case N_FN:   out += "func "; out += v.fn; break;
        case N_OS:   out += "os"; break;
    }
}

static int64_t n_isqrt(int64_t v) {
    if (v < 0) return 0;
    int64_t x = v, y = (v + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

// ---- mini fd table (sysapi.h file API implemented on the VFS) ----
struct NFd { FSNode* node; uint32_t pos; bool writable; };
static List<NFd>* g_nfds = 0;

static int n_fd_open(FSNode* node, bool writable) {
    if (!g_nfds) g_nfds = new List<NFd>();
    for (int i = 0; i < g_nfds->size(); i++) {
        if (!(*g_nfds)[i].node) {
            (*g_nfds)[i].node = node; (*g_nfds)[i].pos = 0; (*g_nfds)[i].writable = writable;
            return i;
        }
    }
    NFd f; f.node = node; f.pos = 0; f.writable = writable;
    g_nfds->push(f);
    return g_nfds->size() - 1;
}
static FSNode* n_fd_node(int fd) {
    if (!g_nfds || fd < 0 || fd >= g_nfds->size()) return 0;
    return (*g_nfds)[fd].node;
}
static void n_fd_close(int fd) {
    if (g_nfds && fd >= 0 && fd < g_nfds->size()) (*g_nfds)[fd].node = 0;
}

// =============================================================================
//  start_r_os: the runtime OS window (empty desktop) + sandbox + autosave
// =============================================================================
struct NOp {
    int kind;            // 0 fill, 1 rect, 2 line, 3 circle, 4 text, 5 pixel
    int x, y, w, h;
    uint32_t color;
    String text;
    int r;
};

struct NTimer { int ms; uint32_t next; String cb; };

struct NOS {
    Window* win;
    NCtx* ctx;
    uint32_t bg;
    List<NOp> ops;
    String title;
    bool closed;
    String sandbox;         // "/home/user/ncode/<name>"
    uint32_t last_save;
    List<NTimer> timers;
    String key_cb;
    String mouse_cb;

    NOS() : win(0), ctx(0), bg(0x20242Fu), closed(false), last_save(0) {}
};

static unsigned n_strtou_hex(const char* s) {
    unsigned v = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') v = v * 16 + (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') v = v * 16 + (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') v = v * 16 + (unsigned)(*s - 'A' + 10);
        else break;
        s++;
    }
    return v;
}

// ---- sandbox: only user-local files under /home/user/ncode/<app>/ ----
static bool n_sandbox_safe(const char* file) {
    if (!file || !*file) return false;
    if (file[0] == '/' || file[0] == '\\') return false;
    for (const char* p = file; *p; p++) {
        if (*p == '/' || *p == '\\') return false;   // no sub-directories
    }
    return nefu::strstr(file, "..") == 0;
}

static String n_sandbox_path(const NOS* os, const char* file) {
    String p = os->sandbox;
    p += "/";
    p += file;
    return p;
}

// ---- desktop serialize / restore (autosave format) ----
static void n_os_serialize(const NOS* os, String& out) {
    out += "#ncode-desktop v1\n";
    char buf[64];
    ksprintf(buf, sizeof(buf), "bg %08X\n", (unsigned)os->bg);
    out += buf;
    if (!os->title.empty()) {
        out += "title ";
        out += os->title;
        out += "\n";
    }
    for (int i = 0; i < os->ops.size(); i++) {
        const NOp& o = os->ops[i];
        switch (o.kind) {
            case 0: ksprintf(buf, sizeof(buf), "fill %d %d %d %d %08X\n", o.x, o.y, o.w, o.h, (unsigned)o.color); out += buf; break;
            case 1: ksprintf(buf, sizeof(buf), "rect %d %d %d %d %08X\n", o.x, o.y, o.w, o.h, (unsigned)o.color); out += buf; break;
            case 2: ksprintf(buf, sizeof(buf), "line %d %d %d %d %08X\n", o.x, o.y, o.w, o.h, (unsigned)o.color); out += buf; break;
            case 3: ksprintf(buf, sizeof(buf), "circle %d %d %d %08X\n", o.x, o.y, o.r, (unsigned)o.color); out += buf; break;
            case 4: {
                ksprintf(buf, sizeof(buf), "text %d %d %08X ", o.x, o.y, (unsigned)o.color);
                out += buf;
                for (int k = 0; k < o.text.len(); k++) {
                    char ch = o.text[k];
                    if (ch == '\\') out += "\\\\";
                    else if (ch == '\n') out += "\\n";
                    else out += ch;
                }
                out += "\n";
                break;
            }
            case 5: ksprintf(buf, sizeof(buf), "pixel %d %d %08X\n", o.x, o.y, (unsigned)o.color); out += buf; break;
        }
    }
}

static void n_os_restore(NOS* os, const char* txt) {
    int i = 0;
    while (txt[i]) {
        const char* line = txt + i;
        const char* nl = strchr(line, '\n');
        int len = nl ? (int)(nl - line) : (int)nefu::strlen(line);
        if (nl) i = (int)(nl - txt) + 1; else i += len;
        if (len <= 0) continue;
        String l(line, len);
        if (l.find("bg ") == 0) {
            os->bg = n_strtou_hex(l.c_str() + 3);
            continue;
        }
        if (l.find("title ") == 0) {
            os->title = l.substr(6, l.len() - 6);
            continue;
        }
        // split into space-separated fields
        List<String> parts;
        int start = 0;
        for (int k = 0; k <= l.len(); k++) {
            if (k == l.len() || l[k] == ' ') {
                if (k > start) parts.push(l.substr(start, k - start));
                start = k + 1;
            }
        }
        if (parts.size() < 2) continue;
        const char* tag = parts[0].c_str();
        NOp o; o.x = 0; o.y = 0; o.w = 0; o.h = 0; o.color = 0; o.r = 0;
        if (nefu::strcmp(tag, "fill") == 0 && parts.size() >= 6) {
            o.kind = 0;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.w = atoi(parts[3].c_str()); o.h = atoi(parts[4].c_str());
            o.color = n_strtou_hex(parts[5].c_str());
            os->ops.push(o);
        } else if (nefu::strcmp(tag, "rect") == 0 && parts.size() >= 6) {
            o.kind = 1;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.w = atoi(parts[3].c_str()); o.h = atoi(parts[4].c_str());
            o.color = n_strtou_hex(parts[5].c_str());
            os->ops.push(o);
        } else if (nefu::strcmp(tag, "line") == 0 && parts.size() >= 6) {
            o.kind = 2;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.w = atoi(parts[3].c_str()); o.h = atoi(parts[4].c_str());
            o.color = n_strtou_hex(parts[5].c_str());
            os->ops.push(o);
        } else if (nefu::strcmp(tag, "circle") == 0 && parts.size() >= 5) {
            o.kind = 3;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.r = atoi(parts[3].c_str());
            o.color = n_strtou_hex(parts[4].c_str());
            os->ops.push(o);
        } else if (nefu::strcmp(tag, "text") == 0 && parts.size() >= 5) {
            o.kind = 4;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.color = n_strtou_hex(parts[3].c_str());
            // text = fields 4..end joined with single spaces
            String t;
            for (int k = 4; k < parts.size(); k++) {
                if (k > 4) t += " ";
                t += parts[k];
            }
            String un;
            for (int k = 0; k < t.len(); k++) {
                if (t[k] == '\\' && k + 1 < t.len()) {
                    if (t[k + 1] == 'n') { un += '\n'; k++; }
                    else if (t[k + 1] == '\\') { un += '\\'; k++; }
                    else un += t[k];
                } else un += t[k];
            }
            o.text = un;
            os->ops.push(o);
        } else if (nefu::strcmp(tag, "pixel") == 0 && parts.size() >= 4) {
            o.kind = 5;
            o.x = atoi(parts[1].c_str()); o.y = atoi(parts[2].c_str());
            o.color = n_strtou_hex(parts[3].c_str());
            os->ops.push(o);
        }
    }
}

#ifndef NCODE_HOST_TEST
// ---- window callbacks (GUI) ----
static void n_os_paint(Window* w) {
    NOS* os = (NOS*)w->userdata;
    if (!os) return;
    Surface& s = w->back;
    s.fill(os->bg);
    for (int i = 0; i < os->ops.size(); i++) {
        const NOp& o = os->ops[i];
        switch (o.kind) {
            case 0: gfx::fillrect(s, o.x, o.y, o.w, o.h, o.color); break;
            case 1: gfx::rect(s, o.x, o.y, o.w, o.h, o.color); break;
            case 2: gfx::line(s, o.x, o.y, o.w, o.h, o.color); break;
            case 3: gfx::circle(s, o.x, o.y, o.r, o.color); break;
            case 4: gfx::text_ttf(s, o.x, o.y, o.text.c_str(), o.color, os->bg, 16); break;
            case 5: gfx::pixel(s, o.x, o.y, o.color); break;
        }
    }
}

static void n_os_tick(Window* w) {
    NOS* os = (NOS*)w->userdata;
    if (!os || os->closed || !os->ctx) return;
    NCtx* ctx = os->ctx;
    uint32_t now = platform_tick_ms();
    // autosave every 0.5 s
    if ((int32_t)(now - os->last_save) >= 500) {
        os->last_save = now;
        String ser;
        n_os_serialize(os, ser);
        FSNode* d = g_vfs->resolve(os->sandbox.c_str());
        if (!d) {
            g_vfs->mkdir("/home/user/ncode");
            g_vfs->mkdir(os->sandbox.c_str());
        }
        FSNode* sf = g_vfs->create_file(n_sandbox_path(os, "desktop.save").c_str());
        if (sf) g_vfs->write_file(sf, (const uint8_t*)ser.c_str(), (uint32_t)ser.len());
    }
    // timers
    for (int i = 0; i < os->timers.size(); i++) {
        NTimer& tm = os->timers[i];
        if ((int32_t)(now - tm.next) >= 0) {
            tm.next = now + (uint32_t)tm.ms;
            n_call_func(ctx, ctx->env, tm.cb.c_str(), 0, 0);
        }
    }
}

static void n_os_key(Window* w, const KeyEvent* e) {
    NOS* os = (NOS*)w->userdata;
    if (!os || !os->ctx || os->key_cb.empty()) return;
    NCtx* ctx = os->ctx;
    NVal args[3];
    args[0] = nv_int(e->keycode);
    args[1] = nv_int(e->ascii);
    args[2] = nv_bool(e->down);
    n_call_func(ctx, ctx->env, os->key_cb.c_str(), 3, args);
}

static void n_os_mouse(Window* w, int mx, int my, uint8_t buttons) {
    NOS* os = (NOS*)w->userdata;
    if (!os || !os->ctx || os->mouse_cb.empty()) return;
    NCtx* ctx = os->ctx;
    NVal args[3];
    args[0] = nv_int(mx);
    args[1] = nv_int(my);
    args[2] = nv_int((int)buttons);
    n_call_func(ctx, ctx->env, os->mouse_cb.c_str(), 3, args);
}

static void n_os_close(Window* w) {
    NOS* os = (NOS*)w->userdata;
    if (os) os->closed = true;
}
#endif // !NCODE_HOST_TEST

// ---- os method dispatch ----
static bool n_os_method(NCtx* ctx, NOS* os, const char* m, int argc, NVal* argv, NVal* out) {
    if (!os) {
        ctx_pln(ctx, "ncode: os: runtime OS window is closed");
        *out = nv_null();
        return true;
    }
    if (os->closed) {
        if (nefu::strcmp(m, "close") == 0) { os->closed = true; *out = nv_null(); return true; }
        ctx_pln(ctx, "ncode: os: runtime OS window is closed");
        *out = nv_null();
        return true;
    }
    if (nefu::strcmp(m, "title") == 0) {
        if (argc >= 1) { String t; nval_to_str(argv[0], t); os->title = t; }
        if (os->win) os->win->title = os->title;
        *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "background") == 0) {
        if (argc >= 1) os->bg = (uint32_t)nv_as_int(argv[0]);
        *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "clear") == 0) { os->ops.clear(); *out = nv_null(); return true; }
    if (nefu::strcmp(m, "fill") == 0 && argc >= 5) {
        NOp o; o.kind = 0; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.w = (int)nv_as_int(argv[2]); o.h = (int)nv_as_int(argv[3]);
        o.color = (uint32_t)nv_as_int(argv[4]); o.r = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "rect") == 0 && argc >= 5) {
        NOp o; o.kind = 1; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.w = (int)nv_as_int(argv[2]); o.h = (int)nv_as_int(argv[3]);
        o.color = (uint32_t)nv_as_int(argv[4]); o.r = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "line") == 0 && argc >= 5) {
        NOp o; o.kind = 2; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.w = (int)nv_as_int(argv[2]); o.h = (int)nv_as_int(argv[3]);
        o.color = (uint32_t)nv_as_int(argv[4]); o.r = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "circle") == 0 && argc >= 4) {
        NOp o; o.kind = 3; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.r = (int)nv_as_int(argv[2]); o.color = (uint32_t)nv_as_int(argv[3]);
        o.w = o.h = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "pixel") == 0 && argc >= 3) {
        NOp o; o.kind = 5; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.color = (uint32_t)nv_as_int(argv[2]); o.w = o.h = 0; o.r = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "text") == 0 && argc >= 4) {
        NOp o; o.kind = 4; o.x = (int)nv_as_int(argv[0]); o.y = (int)nv_as_int(argv[1]);
        o.color = (uint32_t)nv_as_int(argv[2]);
        String t; nval_to_str(argv[3], t);
        o.text = t; o.w = o.h = 0; o.r = 0;
        os->ops.push(o); *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "save") == 0 && argc >= 2) {
        if (argv[0].tag != N_STR || !n_sandbox_safe(argv[0].s.c_str())) {
            ctx_pln(ctx, "ncode: os.save: sandbox allows only plain file names under /home/user/ncode/<app>/");
            *out = nv_bool(false); return true;
        }
        String content;
        nval_to_str(argv[1], content);
        FSNode* d = g_vfs->resolve(os->sandbox.c_str());
        if (!d) { g_vfs->mkdir("/home/user/ncode"); g_vfs->mkdir(os->sandbox.c_str()); }
        FSNode* f = g_vfs->create_file(n_sandbox_path(os, argv[0].s.c_str()).c_str());
        bool ok = f && g_vfs->write_file(f, (const uint8_t*)content.c_str(), (uint32_t)content.len());
        *out = nv_bool(ok); return true;
    }
    if (nefu::strcmp(m, "load") == 0 && argc >= 1) {
        if (argv[0].tag != N_STR || !n_sandbox_safe(argv[0].s.c_str())) {
            ctx_pln(ctx, "ncode: os.load: sandbox allows only plain file names");
            *out = nv_str(""); return true;
        }
        FSNode* f = g_vfs->resolve(n_sandbox_path(os, argv[0].s.c_str()).c_str());
        if (!f || f->is_dir) { *out = nv_str(""); return true; }
        *out = nv_str(String((const char*)f->data, (int)f->size));
        return true;
    }
    if (nefu::strcmp(m, "list") == 0) {
        NVal arr = nv_arr();
        FSNode* d = g_vfs->resolve(os->sandbox.c_str());
        if (d && d->is_dir) {
            for (int i = 0; i < d->children.size(); i++) arr.arr.push(nv_str(d->children[i]->name));
        }
        *out = arr; return true;
    }
    if (nefu::strcmp(m, "remove") == 0 && argc >= 1) {
        if (argv[0].tag != N_STR || !n_sandbox_safe(argv[0].s.c_str())) {
            ctx_pln(ctx, "ncode: os.remove: sandbox allows only plain file names");
            *out = nv_bool(false); return true;
        }
        FSNode* f = g_vfs->resolve(n_sandbox_path(os, argv[0].s.c_str()).c_str());
        bool ok = f && g_vfs->remove_node(f);
        *out = nv_bool(ok); return true;
    }
    if (nefu::strcmp(m, "on_key") == 0 && argc >= 1) {
        String cb; nval_to_str(argv[0], cb);
        os->key_cb = cb; *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "on_mouse") == 0 && argc >= 1) {
        String cb; nval_to_str(argv[0], cb);
        os->mouse_cb = cb; *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "set_interval") == 0 && argc >= 2) {
        NTimer tm;
        tm.ms = (int)nv_as_int(argv[0]);
        if (tm.ms < 10) tm.ms = 10;
        String cb; nval_to_str(argv[1], cb);
        tm.cb = cb;
        tm.next = platform_tick_ms() + (uint32_t)tm.ms;
        os->timers.push(tm);
        *out = nv_null(); return true;
    }
    if (nefu::strcmp(m, "width") == 0) {
#ifndef NCODE_HOST_TEST
        *out = nv_int(os->win ? os->win->content_w : 640);
#else
        *out = nv_int(640);
#endif
        return true;
    }
    if (nefu::strcmp(m, "height") == 0) {
#ifndef NCODE_HOST_TEST
        *out = nv_int(os->win ? os->win->content_h : 400);
#else
        *out = nv_int(400);
#endif
        return true;
    }
    if (nefu::strcmp(m, "close") == 0) {
        os->closed = true;
#ifndef NCODE_HOST_TEST
        if (os->win) g_wm->close_window(os->win);
#endif
        *out = nv_null(); return true;
    }
    ctx_pln(ctx, "ncode: os: unknown method");
    *out = nv_null();
    return true;
}

// ---- start_r_os() ----
static NVal n_start_r_os(NCtx* ctx) {
#ifndef NCODE_HOST_TEST
    if (ctx->os) {
        ctx_pln(ctx, "ncode: start_r_os: a runtime OS window is already running");
        return nv_null();
    }
    NOS* os = new NOS();
    os->ctx = ctx;
    os->sandbox = "/home/user/ncode";
    os->sandbox += "/";
    os->sandbox += ctx->sandbox;
    g_vfs->mkdir("/home/user/ncode");
    FSNode* d = g_vfs->resolve(os->sandbox.c_str());
    if (!d) g_vfs->mkdir(os->sandbox.c_str());
    FSNode* sf = g_vfs->resolve(n_sandbox_path(os, "desktop.save").c_str());
    if (sf && !sf->is_dir && sf->size > 0) {
        String ser((const char*)sf->data, (int)sf->size);
        n_os_restore(os, ser.c_str());
    }
    int x = 40, y = 40;
    Window* w = g_wm->create_window(os->title.empty() ? "nefu runtime OS" : os->title.c_str(),
                                    x, y, 640, 400);
    if (!w) { delete os; return nv_null(); }
    os->win = w;
    w->userdata = os;
    w->on_paint = n_os_paint;
    w->on_tick = n_os_tick;
    w->on_key = n_os_key;
    w->on_mouse = n_os_mouse;
    w->on_close = n_os_close;
    w->esc_close = true;
    ctx->os = os;
    os->last_save = platform_tick_ms();
    return nv_os(os);
#else
    (void)ctx;
    ctx_pln(ctx, "ncode: start_r_os: not available in host test mode");
    return nv_null();
#endif
}

// =============================================================================
//  member dispatch (strings / arrays / os)
// =============================================================================
static NVal n_member(NCtx* ctx, const NVal& obj, const char* m, int argc, NVal* argv, bool is_call) {
    NVal out = nv_null();
    if (obj.tag == N_STR) {
        if (!ctx->has_std) {
            ctx_pln(ctx, "ncode: import<std> required for string/array helpers");
            return out;
        }
        const String& s = obj.s;
        if (nefu::strcmp(m, "len") == 0) { out = nv_int(s.len()); return out; }
        if (nefu::strcmp(m, "upper") == 0) {
            String r;
            for (int i = 0; i < s.len(); i++) {
                char c = s[i];
                if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                r += c;
            }
            out = nv_str(r); return out;
        }
        if (nefu::strcmp(m, "lower") == 0) {
            String r;
            for (int i = 0; i < s.len(); i++) {
                char c = s[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                r += c;
            }
            out = nv_str(r); return out;
        }
        if (nefu::strcmp(m, "substr") == 0 && argc >= 2) {
            int a = (int)nv_as_int(argv[0]);
            int b = (int)nv_as_int(argv[1]);
            if (a < 0) a = 0;
            if (b < 0) b = 0;
            out = nv_str(s.substr(a, b));
            return out;
        }
        if (nefu::strcmp(m, "find") == 0 && argc >= 1) {
            String sub; nval_to_str(argv[0], sub);
            out = nv_int(s.find(sub.c_str()));
            return out;
        }
        if (nefu::strcmp(m, "contains") == 0 && argc >= 1) {
            String sub; nval_to_str(argv[0], sub);
            out = nv_bool(s.find(sub.c_str()) >= 0);
            return out;
        }
        if (nefu::strcmp(m, "split") == 0 && argc >= 1) {
            String sep; nval_to_str(argv[0], sep);
            NVal arr = nv_arr();
            if (sep.len() == 0) { arr.arr.push(nv_str(s)); return arr; }
            int start = 0;
            for (int i = 0; i <= s.len() - sep.len(); i++) {
                bool match = true;
                for (int j = 0; j < sep.len(); j++) {
                    if (s[i + j] != sep[j]) { match = false; break; }
                }
                if (match) {
                    arr.arr.push(nv_str(s.substr(start, i - start)));
                    i += sep.len() - 1;
                    start = i + 1;
                }
            }
            arr.arr.push(nv_str(s.substr(start, s.len() - start)));
            return arr;
        }
        if (nefu::strcmp(m, "trim") == 0) {
            int a = 0, b = s.len();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n')) a++;
            while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\n')) b--;
            out = nv_str(s.substr(a, b - a));
            return out;
        }
        (void)is_call;
        ctx_pln(ctx, "ncode: string has no member");
        return out;
    }
    if (obj.tag == N_ARR) {
        if (!ctx->has_std) {
            ctx_pln(ctx, "ncode: import<std> required for string/array helpers");
            return out;
        }
        if (nefu::strcmp(m, "len") == 0) { out = nv_int(obj.arr.size()); return out; }
        if (nefu::strcmp(m, "push") == 0 || nefu::strcmp(m, "pop") == 0) {
            // mutation happens in K_MEMBER eval on the variable binding
            out = obj;
            return out;
        }
        if (nefu::strcmp(m, "get") == 0 && argc >= 1) {
            int idx = (int)nv_as_int(argv[0]);
            if (idx >= 0 && idx < obj.arr.size()) out = obj.arr[idx];
            return out;
        }
        if (nefu::strcmp(m, "join") == 0 && argc >= 1) {
            String sep; nval_to_str(argv[0], sep);
            String r;
            for (int i = 0; i < obj.arr.size(); i++) {
                if (i > 0) r += sep;
                nval_to_str(obj.arr[i], r);
            }
            out = nv_str(r);
            return out;
        }
        (void)is_call;
        ctx_pln(ctx, "ncode: array has no member");
        return out;
    }
    if (obj.tag == N_OS) {
        if (!ctx->has_std) {
            ctx_pln(ctx, "ncode: import<std> required for start_r_os");
            return out;
        }
        n_os_method(ctx, (NOS*)obj.os, m, argc, argv, &out);
        return out;
    }
    (void)is_call;
    ctx_pln(ctx, "ncode: member access on unsupported value");
    return out;
}

// =============================================================================
//  builtins
// =============================================================================
static bool app_launch_by_name(const char* n) {
#ifndef NCODE_HOST_TEST
    int id = nefud_name_to_app_id(n);
    if (id < 0) return false;
    app_launch(id);
    return true;
#else
    (void)n;
    return false;
#endif
}

static NVal n_builtin(NCtx* ctx, const char* name, int argc, NVal* argv, bool* found) {
    *found = true;
    NVal out = nv_null();

    // ---- std (import<std>) ----
    if (!ctx->has_std) { *found = false; return out; }
    if (nefu::strcmp(name, "print") == 0 || nefu::strcmp(name, "println") == 0) {
        bool nl = nefu::strcmp(name, "println") == 0;
        String line;
        for (int i = 0; i < argc; i++) {
            if (i > 0) line += " ";
            nval_to_str(argv[i], line);
        }
        if (nl) ctx_pln(ctx, line.c_str());
        else ctx_print(ctx, line.c_str());
        return out;
    }
    if (nefu::strcmp(name, "len") == 0) {
        if (argc >= 1) {
            if (argv[0].tag == N_STR) out = nv_int(argv[0].s.len());
            else if (argv[0].tag == N_ARR) out = nv_int(argv[0].arr.size());
            else out = nv_int(0);
        }
        return out;
    }
    if (nefu::strcmp(name, "str") == 0) {
        if (argc >= 1) { String s; nval_to_str(argv[0], s); out = nv_str(s); }
        return out;
    }
    if (nefu::strcmp(name, "int") == 0) {
        if (argc >= 1) {
            if (argv[0].tag == N_STR) out = nv_int(atoi(argv[0].s.c_str()));
            else if (nv_is_num(argv[0])) out = nv_int(nv_as_int(argv[0]));
            else out = nv_int(0);
        }
        return out;
    }
    if (nefu::strcmp(name, "float") == 0) {
        if (argc >= 1) {
            if (argv[0].tag == N_STR) out = nv_float((double)atoi(argv[0].s.c_str()));
            else if (nv_is_num(argv[0])) out = nv_float(nv_as_num(argv[0]));
            else out = nv_float(0.0);
        }
        return out;
    }
    if (nefu::strcmp(name, "abs") == 0) {
        if (argc >= 1 && argv[0].tag == N_FLOAT) out = nv_float(argv[0].f < 0 ? -argv[0].f : argv[0].f);
        else if (argc >= 1 && argv[0].tag == N_INT) out = nv_int(argv[0].i < 0 ? -argv[0].i : argv[0].i);
        return out;
    }
    if (nefu::strcmp(name, "min") == 0 || nefu::strcmp(name, "max") == 0) {
        if (argc >= 2) {
            bool is_max = name[0] == 'm' && name[1] == 'a';
            if (argv[0].tag == N_FLOAT || argv[1].tag == N_FLOAT) {
                double a = nv_as_num(argv[0]), b = nv_as_num(argv[1]);
                out = nv_float(is_max ? (a > b ? a : b) : (a < b ? a : b));
            } else {
                int64_t a = nv_as_int(argv[0]), b = nv_as_int(argv[1]);
                out = nv_int(is_max ? (a > b ? a : b) : (a < b ? a : b));
            }
        }
        return out;
    }
    if (nefu::strcmp(name, "sqrt") == 0) {
        if (argc >= 1) {
            if (argv[0].tag == N_FLOAT) {
                double v = argv[0].f;
                if (v < 0) v = 0;
                double x = v;
                for (int i = 0; i < 20; i++) x = 0.5 * (x + v / x);
                out = nv_float(x);
            } else {
                out = nv_int(n_isqrt(nv_as_int(argv[0])));
            }
        }
        return out;
    }
    if (nefu::strcmp(name, "pow") == 0 && argc >= 2) {
        double a = nv_as_num(argv[0]);
        int64_t b = nv_as_int(argv[1]);
        double r = 1.0;
        bool neg = b < 0;
        if (neg) b = -b;
        for (int64_t i = 0; i < b && i < 100000; i++) r *= a;
        out = nv_float(neg ? 1.0 / r : r);
        return out;
    }
    if (nefu::strcmp(name, "rand") == 0) {
        ctx->rng = ctx->rng * 1664525u + 1013904223u;
        int64_t n = argc >= 1 ? nv_as_int(argv[0]) : 0;
        if (n <= 0) out = nv_int((int64_t)(ctx->rng % 2147483647u));
        else out = nv_int((int64_t)(ctx->rng % (uint32_t)n));
        return out;
    }
    if (nefu::strcmp(name, "time_ms") == 0) {
        out = nv_int((int64_t)platform_tick_ms());
        return out;
    }
    if (nefu::strcmp(name, "sleep") == 0) {
        if (argc >= 1) platform_thread_sleep((uint32_t)nv_as_int(argv[0]));
        return out;
    }
    if (nefu::strcmp(name, "args") == 0) {
        NVal arr = nv_arr();
        for (int i = 0; i < ctx->args.size(); i++) arr.arr.push(nv_str(ctx->args[i]));
        return arr;
    }
    if (nefu::strcmp(name, "type") == 0) {
        if (argc >= 1) {
            switch (argv[0].tag) {
                case N_NULL: out = nv_str("null"); break;
                case N_BOOL: out = nv_str("bool"); break;
                case N_INT:  out = nv_str("int"); break;
                case N_FLOAT:out = nv_str("float"); break;
                case N_STR:  out = nv_str("str"); break;
                case N_ARR:  out = nv_str("arr"); break;
                case N_FN:   out = nv_str("func"); break;
                case N_OS:   out = nv_str("os"); break;
            }
        }
        return out;
    }
    if (nefu::strcmp(name, "start_r_os") == 0) {
        if (!ctx->has_std) { ctx_pln(ctx, "ncode: import<std> required for start_r_os"); return out; }
        return n_start_r_os(ctx);
    }

    // ---- sysapi (import<sysapi>; sysapi.h API set, implemented on the VFS) ----
    if (!ctx->has_sysapi) {
        *found = false;
        return out;
    }

    if (nefu::strcmp(name, "file_open") == 0 && argc >= 2) {
        String p; nval_to_str(argv[0], p);
        FSNode* f = g_vfs->resolve(p.c_str());
        int flags = (int)nv_as_int(argv[1]);
        bool write = (flags & 1) != 0;
        if ((!f || f->is_dir) && write && !f) {
            f = g_vfs->create_file(p.c_str());   // create for writing
        }
        if (!f || f->is_dir) { out = nv_int(-1); return out; }
        out = nv_int(n_fd_open(f, write));
        return out;
    }
    if (nefu::strcmp(name, "file_close") == 0 && argc >= 1) {
        n_fd_close((int)nv_as_int(argv[0]));
        return out;
    }
    if (nefu::strcmp(name, "file_read") == 0 && argc >= 2) {
        int fd = (int)nv_as_int(argv[0]);
        int count = (int)nv_as_int(argv[1]);
        FSNode* f = n_fd_node(fd);
        if (!f || count <= 0 || !g_nfds || fd < 0 || fd >= g_nfds->size()) { out = nv_str(""); return out; }
        uint32_t pos = (*g_nfds)[fd].pos;
        if (pos >= f->size) { out = nv_str(""); return out; }
        if (count > (int)(f->size - pos)) count = (int)(f->size - pos);
        String r((const char*)f->data + pos, count);
        (*g_nfds)[fd].pos += (uint32_t)count;
        out = nv_str(r);
        return out;
    }
    if (nefu::strcmp(name, "file_write") == 0 && argc >= 2) {
        int fd = (int)nv_as_int(argv[0]);
        String data; nval_to_str(argv[1], data);
        FSNode* f = n_fd_node(fd);
        if (!f || !g_nfds || fd < 0 || fd >= g_nfds->size()) { out = nv_int(-1); return out; }
        uint32_t pos = (*g_nfds)[fd].pos;
        uint32_t end = pos + (uint32_t)data.len();
        uint32_t newlen = end > f->size ? end : f->size;
        uint8_t* nb = (uint8_t*)kalloc(newlen > 0 ? newlen : 1);
        if (!nb) { out = nv_int(-1); return out; }
        if (f->size > 0 && f->data) nefu::memcpy(nb, f->data, f->size);
        if (data.len() > 0) nefu::memcpy(nb + pos, data.c_str(), data.len());
        g_vfs->write_file(f, nb, newlen);
        kfree(nb);
        (*g_nfds)[fd].pos += (uint32_t)data.len();
        out = nv_int(data.len());
        return out;
    }
    if (nefu::strcmp(name, "file_seek") == 0 && argc >= 3) {
        int fd = (int)nv_as_int(argv[0]);
        int off = (int)nv_as_int(argv[1]);
        int whence = (int)nv_as_int(argv[2]);
        FSNode* f = n_fd_node(fd);
        if (!f || !g_nfds || fd < 0 || fd >= g_nfds->size()) { out = nv_int(-1); return out; }
        int32_t pos = 0;
        if (whence == 0) pos = off;
        else if (whence == 1) pos = (int32_t)(*g_nfds)[fd].pos + off;
        else pos = (int32_t)f->size + off;
        if (pos < 0) pos = 0;
        if ((uint32_t)pos > f->size) pos = (int32_t)f->size;
        (*g_nfds)[fd].pos = (uint32_t)pos;
        out = nv_int(pos);
        return out;
    }
    if (nefu::strcmp(name, "file_size") == 0 && argc >= 1) {
        FSNode* f = n_fd_node((int)nv_as_int(argv[0]));
        out = nv_int(f ? (int64_t)f->size : 0);
        return out;
    }
    if (nefu::strcmp(name, "file_exists") == 0 && argc >= 1) {
        String p; nval_to_str(argv[0], p);
        FSNode* f = g_vfs->resolve(p.c_str());
        out = nv_bool(f && !f->is_dir);
        return out;
    }
    if (nefu::strcmp(name, "file_read_all") == 0 && argc >= 1) {
        String p; nval_to_str(argv[0], p);
        FSNode* f = g_vfs->resolve(p.c_str());
        if (!f || f->is_dir) { out = nv_str(""); return out; }
        out = nv_str(String((const char*)f->data, (int)f->size));
        return out;
    }
    if (nefu::strcmp(name, "file_write_all") == 0 && argc >= 2) {
        String p; nval_to_str(argv[0], p);
        String data; nval_to_str(argv[1], data);
        FSNode* f = g_vfs->resolve(p.c_str());
        if (!f) f = g_vfs->create_file(p.c_str());
        if (!f) { out = nv_int(-1); return out; }
        g_vfs->write_file(f, (const uint8_t*)data.c_str(), (uint32_t)data.len());
        out = nv_int(data.len());
        return out;
    }
    if (nefu::strcmp(name, "dir_mkdir") == 0 && argc >= 1) {
        String p; nval_to_str(argv[0], p);
        out = nv_bool(g_vfs->mkdir(p.c_str()) != 0);
        return out;
    }
    if (nefu::strcmp(name, "dir_list") == 0 && argc >= 1) {
        String p; nval_to_str(argv[0], p);
        NVal arr = nv_arr();
        FSNode* d = g_vfs->resolve(p.c_str());
        if (d && d->is_dir) {
            for (int i = 0; i < d->children.size(); i++) arr.arr.push(nv_str(d->children[i]->name));
        }
        return arr;
    }
    if (nefu::strcmp(name, "file_remove") == 0 && argc >= 1) {
        String p; nval_to_str(argv[0], p);
        FSNode* f = g_vfs->resolve(p.c_str());
        out = nv_bool(f && g_vfs->remove_node(f));
        return out;
    }
    if (nefu::strcmp(name, "file_rename") == 0 && argc >= 2) {
        String a; nval_to_str(argv[0], a);
        String b; nval_to_str(argv[1], b);
        FSNode* f = g_vfs->resolve(a.c_str());
        FSNode* d = g_vfs->resolve(b.c_str());
        if (d && !d->is_dir) { out = nv_bool(false); return out; }
        out = nv_bool(f && g_vfs->move_node(f, d ? d : g_vfs->cwd, 0) != 0);
        return out;
    }
    // process
    if (nefu::strcmp(name, "process_pid") == 0) {
        out = nv_int(nefu::sysapi::process_pid());
        return out;
    }
    if (nefu::strcmp(name, "process_exit") == 0) {
        ctx->stop = true;
        ctx->ctl = 1;
        ctx->ret = nv_int(argc >= 1 ? nv_as_int(argv[0]) : 0);
        return out;
    }
    // network
    if (nefu::strcmp(name, "http_get") == 0 && argc >= 1) {
        String url; nval_to_str(argv[0], url);
        uint8_t* body = 0;
        uint32_t blen = 0;
        if (nefu::sysapi::http_get(url.c_str(), &body, &blen) && body) {
            out = nv_str(String((const char*)body, (int)blen));
            kfree(body);
        } else {
            out = nv_str("");
        }
        return out;
    }
    if (nefu::strcmp(name, "network_available") == 0) {
        out = nv_bool(nefu::sysapi::network_available());
        return out;
    }
    // display
    if (nefu::strcmp(name, "screen_width") == 0) { out = nv_int(nefu::sysapi::screen_width()); return out; }
    if (nefu::strcmp(name, "screen_height") == 0) { out = nv_int(nefu::sysapi::screen_height()); return out; }
    if (nefu::strcmp(name, "screen_bpp") == 0) { out = nv_int(nefu::sysapi::screen_bpp()); return out; }
    // power
    if (nefu::strcmp(name, "power_shutdown") == 0) { nefu::sysapi::power_shutdown(); return out; }
    if (nefu::strcmp(name, "power_reboot") == 0) { nefu::sysapi::power_reboot(); return out; }
    if (nefu::strcmp(name, "power_reboot_to_bios") == 0) { nefu::sysapi::power_reboot_to_bios(); return out; }
    if (nefu::strcmp(name, "power_suspend") == 0) { nefu::sysapi::power_suspend(); return out; }
    // settings
    if (nefu::strcmp(name, "settings_get") == 0 && argc >= 1) {
        String k; nval_to_str(argv[0], k);
        out = nv_str(nefu::sysapi::settings_get(k.c_str()));
        return out;
    }
    if (nefu::strcmp(name, "settings_set") == 0 && argc >= 2) {
        String k; nval_to_str(argv[0], k);
        String v; nval_to_str(argv[1], v);
        nefu::sysapi::settings_set(k.c_str(), v.c_str());
        return out;
    }
    if (nefu::strcmp(name, "settings_save") == 0) {
        nefu::sysapi::settings_save();
        return out;
    }
    // time
    if (nefu::strcmp(name, "time_now") == 0) { out = nv_int((int64_t)nefu::sysapi::time_now()); return out; }
    if (nefu::strcmp(name, "time_uptime") == 0) { out = nv_int((int64_t)nefu::sysapi::time_uptime()); return out; }
    // notification
    if (nefu::strcmp(name, "notify") == 0 && argc >= 2) {
        String t; nval_to_str(argv[0], t);
        String m; nval_to_str(argv[1], m);
        nefu::sysapi::notify(t.c_str(), m.c_str());
        return out;
    }
    if (nefu::strcmp(name, "notification_count") == 0) { out = nv_int(nefu::sysapi::notification_count()); return out; }
    if (nefu::strcmp(name, "notification_mark_all_read") == 0) { nefu::sysapi::notification_mark_all_read(); return out; }
    if (nefu::strcmp(name, "notification_clear") == 0) { nefu::sysapi::notification_clear(); return out; }
    // app
    if (nefu::strcmp(name, "app_launch") == 0 && argc >= 1) {
        if (argv[0].tag == N_INT) out = nv_bool(nefu::sysapi::app_launch((int)argv[0].i));
        else {
            String n; nval_to_str(argv[0], n);
            out = nv_bool(app_launch_by_name(n.c_str()));
        }
        return out;
    }
    if (nefu::strcmp(name, "app_list") == 0) {
        NVal arr = nv_arr();
        int ids[64];
        int n = nefu::sysapi::app_list(ids, 64);
        for (int i = 0; i < n && i < 64; i++) arr.arr.push(nv_int(ids[i]));
        return arr;
    }
    if (nefu::strcmp(name, "app_installed") == 0 && argc >= 1) {
        if (argv[0].tag == N_INT) out = nv_bool(nefu::sysapi::app_installed((int)argv[0].i));
        else {
            String n; nval_to_str(argv[0], n);
            int id = nefud_name_to_app_id(n.c_str());
            out = nv_bool(id >= 0 ? nefu::sysapi::app_installed(id) : false);
        }
        return out;
    }

    *found = false;
    return out;
}

// =============================================================================
//  eval
// =============================================================================
static NVal n_eval_bin(NCtx* ctx, NEnv* env, NNode* e) {
    if (e->op == 9) {  // AND
        NVal l = n_eval_expr(ctx, env, e->a);
        if (ctx->err) return nv_null();
        return nv_bool(nv_truthy(l) && nv_truthy(n_eval_expr(ctx, env, e->b)));
    }
    if (e->op == 10) { // OR
        NVal l = n_eval_expr(ctx, env, e->a);
        if (ctx->err) return nv_null();
        return nv_bool(nv_truthy(l) || nv_truthy(n_eval_expr(ctx, env, e->b)));
    }
    NVal l = n_eval_expr(ctx, env, e->a);
    if (ctx->err) return nv_null();
    NVal r = n_eval_expr(ctx, env, e->b);
    if (ctx->err) return nv_null();

    if (e->op >= 1 && e->op <= 6) {
        bool res = false;
        if (l.tag == N_STR && r.tag == N_STR) {
            int c = nefu::strcmp(l.s.c_str(), r.s.c_str());
            switch (e->op) {
                case 1: res = c == 0; break;
                case 2: res = c != 0; break;
                case 3: res = c < 0; break;
                case 4: res = c > 0; break;
                case 5: res = c <= 0; break;
                case 6: res = c >= 0; break;
            }
        } else if (l.tag == N_BOOL && r.tag == N_BOOL) {
            switch (e->op) {
                case 1: res = l.b == r.b; break;
                case 2: res = l.b != r.b; break;
                default: res = false; break;
            }
        } else if (nv_is_num(l) && nv_is_num(r)) {
            double a = nv_as_num(l), b = nv_as_num(r);
            switch (e->op) {
                case 1: res = a == b; break;
                case 2: res = a != b; break;
                case 3: res = a < b; break;
                case 4: res = a > b; break;
                case 5: res = a <= b; break;
                case 6: res = a >= b; break;
            }
        } else {
            bool a = nv_truthy(l), b = nv_truthy(r);
            switch (e->op) {
                case 1: res = a == b; break;
                case 2: res = a != b; break;
                default: res = false; break;
            }
        }
        return nv_bool(res);
    }

    if (e->op == 7 || e->op == 8 || e->op == 11 || e->op == 12 || e->op == 13) {
        if (e->op == 7 && (l.tag == N_STR || r.tag == N_STR)) {
            String s;
            nval_to_str(l, s);
            nval_to_str(r, s);
            return nv_str(s);
        }
        if (nv_is_num(l) && nv_is_num(r)) {
            bool isf = (l.tag == N_FLOAT || r.tag == N_FLOAT);
            if (isf) {
                double a = nv_as_num(l), b = nv_as_num(r), v = 0;
                switch (e->op) {
                    case 7: v = a + b; break;
                    case 8: v = a - b; break;
                    case 11: v = a * b; break;
                    case 12: v = b == 0 ? 0 : a / b; break;
                    case 13: v = b == 0 ? 0 : (double)((int64_t)a % (int64_t)b); break;
                }
                return nv_float(v);
            }
            int64_t a = nv_as_int(l), b = nv_as_int(r), v = 0;
            switch (e->op) {
                case 7: v = a + b; break;
                case 8: v = a - b; break;
                case 11: v = a * b; break;
                case 12: v = b == 0 ? 0 : a / b; break;
                case 13: v = b == 0 ? 0 : a % b; break;
            }
            return nv_int(v);
        }
        ctx_pln(ctx, "ncode: arithmetic on non-numeric values");
        return nv_null();
    }
    ctx_pln(ctx, "ncode: unknown operator");
    return nv_null();
}

static NVal n_eval_expr(NCtx* ctx, NEnv* env, NNode* e) {
    if (!e || ctx->err) return nv_null();
    switch (e->k) {
        case K_LIT: return e->lit;
        case K_VAR: {
            NVal* v = env_find(env, e->name.c_str());
            if (!v) {
                String m = "ncode: undefined variable: ";
                m += e->name;
                ctx_pln(ctx, m.c_str());
                ctx->err = true;
                return nv_null();
            }
            return *v;
        }
        case K_BIN: return n_eval_bin(ctx, env, e);
        case K_UN: {
            NVal v = n_eval_expr(ctx, env, e->a);
            if (ctx->err) return nv_null();
            if (e->op == 14) {
                if (v.tag == N_FLOAT) return nv_float(-v.f);
                if (v.tag == N_INT) return nv_int(-v.i);
                return nv_null();
            }
            return nv_bool(!nv_truthy(v));
        }
        case K_CALL: {
            int argc = e->args.size();
            if (argc > 32) argc = 32;
            NVal argv[32];
            for (int i = 0; i < argc; i++) {
                argv[i] = n_eval_expr(ctx, env, e->args[i]);
                if (ctx->err) return nv_null();
            }
            for (int i = 0; i < ctx->funcs.size(); i++) {
                if (ctx->funcs[i]->name == e->name) {
                    return n_call_func(ctx, env, e->name.c_str(), argc, argv);
                }
            }
            bool found = false;
            NVal r = n_builtin(ctx, e->name.c_str(), argc, argv, &found);
            if (!found) {
                String m = "ncode: unknown function: ";
                m += e->name;
                ctx_pln(ctx, m.c_str());
                ctx->err = true;
                return nv_null();
            }
            return r;
        }
        case K_ASSIGN: {
            NVal v = n_eval_expr(ctx, env, e->b);
            if (ctx->err) return nv_null();
            if (!e->flag) {
                if (!env_set(env, e->name.c_str(), v)) {
                    String m = "ncode: undefined variable in assignment: ";
                    m += e->name;
                    ctx_pln(ctx, m.c_str());
                    ctx->err = true;
                    return nv_null();
                }
                return v;
            }
            // arr[i] = v
            NVal idx = n_eval_expr(ctx, env, e->c);
            if (ctx->err) return nv_null();
            int i = (int)nv_as_int(idx);
            NVal* tgt = 0;
            if (e->a && e->a->k == K_VAR) tgt = env_find(env, e->a->name.c_str());
            if (!tgt || tgt->tag != N_ARR) {
                ctx_pln(ctx, "ncode: index assignment target not found");
                return nv_null();
            }
            if (i >= 0 && i < tgt->arr.size()) tgt->arr[i] = v;
            else ctx_pln(ctx, "ncode: index out of range");
            return v;
        }
        case K_ARR: {
            NVal arr = nv_arr();
            for (int i = 0; i < e->args.size(); i++) {
                arr.arr.push(n_eval_expr(ctx, env, e->args[i]));
                if (ctx->err) return nv_null();
            }
            return arr;
        }
        case K_INDEX: {
            NVal obj = n_eval_expr(ctx, env, e->a);
            if (ctx->err) return nv_null();
            NVal idx = n_eval_expr(ctx, env, e->b);
            if (ctx->err) return nv_null();
            int i = (int)nv_as_int(idx);
            if (obj.tag == N_STR) {
                if (i >= 0 && i < obj.s.len()) {
                    String ch;
                    ch += obj.s[i];
                    return nv_str(ch);
                }
                ctx_pln(ctx, "ncode: string index out of range");
                return nv_str("");
            }
            if (obj.tag != N_ARR) { ctx_pln(ctx, "ncode: index on non-array"); return nv_null(); }
            if (i >= 0 && i < obj.arr.size()) return obj.arr[i];
            ctx_pln(ctx, "ncode: array index out of range");
            return nv_null();
        }
        case K_INCDEC: {
            NVal* v = env_find(env, e->name.c_str());
            if (!v) { ctx_pln(ctx, "ncode: undefined variable in ++/--"); return nv_null(); }
            NVal old = *v;
            if (old.tag == N_INT) { old.i += e->op; }
            else if (old.tag == N_FLOAT) { old.f += (double)e->op; }
            else { ctx_pln(ctx, "ncode: ++/-- on non-numeric"); return nv_null(); }
            *v = old;
            return old;   // postfix semantics: returns the old value
        }
        case K_MEMBER: {
            NVal obj = n_eval_expr(ctx, env, e->a);
            if (ctx->err) return nv_null();
            int argc = e->args.size();
            if (argc > 16) argc = 16;
            NVal argv[16];
            for (int i = 0; i < argc; i++) {
                argv[i] = n_eval_expr(ctx, env, e->args[i]);
                if (ctx->err) return nv_null();
            }
            // arr.push / arr.pop mutate the variable binding
            if (obj.tag == N_ARR && e->a && e->a->k == K_VAR && e->flag &&
                (e->member == "push" || e->member == "pop")) {
                NVal* tgt = env_find(env, e->a->name.c_str());
                NVal r = nv_null();
                if (tgt) {
                    if (e->member == "push" && argc >= 1) { tgt->arr.push(argv[0]); r = *tgt; }
                    else if (e->member == "pop") {
                        if (tgt->arr.size() > 0) r = tgt->arr.pop();
                    }
                }
                return r;
            }
            return n_member(ctx, obj, e->member.c_str(), argc, argv, e->flag);
        }
    }
    return nv_null();
}

static int n_eval_stmt(NCtx* ctx, NEnv* env, NNode* s) {
    if (!s || ctx->err) return ctx->ctl ? ctx->ctl : 0;
    if (ctx->stop) return 1;
    switch (s->k) {
        case K_LET: {
            NVal v = s->b ? n_eval_expr(ctx, env, s->b) : nv_null();
            if (ctx->err) return 0;
            if (env_find(env, s->name.c_str())) env_set(env, s->name.c_str(), v);
            else env_declare(env, s->name.c_str(), v);
            return 0;
        }
        case K_EXPR:
            n_eval_expr(ctx, env, s->a);
            return 0;
        case K_IF: {
            NVal c = n_eval_expr(ctx, env, s->a);
            if (ctx->err) return 0;
            if (nv_truthy(c)) return n_eval_stmt(ctx, env, s->b);
            if (s->c) return n_eval_stmt(ctx, env, s->c);
            return 0;
        }
        case K_WHILE: {
            for (;;) {
                NVal c = n_eval_expr(ctx, env, s->a);
                if (ctx->err) return 0;
                if (!nv_truthy(c)) break;
                int r = n_eval_stmt(ctx, env, s->b);
                if (r == 1) return 1;
                if (r == 2) break;
                if (ctx->err) return 0;
            }
            return 0;
        }
        case K_FOR: {
            NEnv* scope = env_new(env);
            if (s->a) n_eval_stmt(ctx, scope, s->a);
            for (;;) {
                if (ctx->stop) { delete scope; return 1; }
                if (s->b) {
                    NVal c = n_eval_expr(ctx, scope, s->b);
                    if (ctx->err) { delete scope; return 0; }
                    if (!nv_truthy(c)) break;
                }
                int r = n_eval_stmt(ctx, scope, s->stmts.size() > 0 ? s->stmts[0] : 0);
                if (r == 1) { delete scope; return 1; }
                if (r == 2) break;
                if (s->c) n_eval_expr(ctx, scope, s->c);
                if (ctx->err) { delete scope; return 0; }
            }
            delete scope;
            return 0;
        }
        case K_BLOCK: {
            NEnv* scope = env_new(env);
            int r = 0;
            for (int i = 0; i < s->stmts.size() && !ctx->err; i++) {
                r = n_eval_stmt(ctx, scope, s->stmts[i]);
                if (r != 0) break;
            }
            delete scope;
            return r;
        }
        case K_RET:
            ctx->ctl = 1;
            ctx->ret = s->a ? n_eval_expr(ctx, env, s->a) : nv_null();
            return 1;
        case K_BRK: return 2;
        case K_CONT: return 3;
        default:
            n_eval_expr(ctx, env, s);
            return 0;
    }
}

static NVal n_call_func(NCtx* ctx, NEnv* caller, const char* name, int argc, NVal* argv) {
    NNode* fn = 0;
    for (int i = 0; i < ctx->funcs.size(); i++) {
        if (ctx->funcs[i]->name == name) { fn = ctx->funcs[i]; break; }
    }
    if (!fn) {
        bool found = false;
        return n_builtin(ctx, name, argc, argv, &found);
    }
    NEnv* scope = env_new(caller);
    for (int i = 0; i < fn->params.size(); i++) {
        if (i < argc) env_declare(scope, fn->params[i].c_str(), argv[i]);
        else env_declare(scope, fn->params[i].c_str(), nv_null());
    }
    int old_ctl = ctx->ctl;
    NVal old_ret = ctx->ret;
    ctx->ctl = 0;
    n_eval_stmt(ctx, scope, fn->b);
    NVal r = ctx->ctl == 1 ? ctx->ret : nv_null();
    ctx->ctl = old_ctl;
    ctx->ret = old_ret;
    delete scope;
    return r;
}

// =============================================================================
//  entry points
// =============================================================================
static bool n_run(NCtx* ctx, NNode* prog) {
    NNode* main_fn = 0;
    for (int i = 0; i < ctx->funcs.size(); i++) {
        if (ctx->funcs[i]->name == "main") { main_fn = ctx->funcs[i]; break; }
    }
    if (!main_fn) {
        ctx_pln(ctx, "ncode: no main() entry point");
        return false;
    }
    // execute top-level statements first (so globals declared outside
    // functions are visible to main)
    for (int i = 0; i < prog->stmts.size() && !ctx->err; i++) {
        n_eval_stmt(ctx, ctx->env, prog->stmts[i]);
    }
    if (!ctx->err) n_call_func(ctx, ctx->env, "main", 0, 0);
    return !ctx->err;
}

static String n_script_sandbox_name(const char* path) {
    if (!path || !*path) return "app";
    // npkg packages live under /pkg/<name>/ -> sandbox is the package name
    const char* pkg = nefu::strstr(path, "/pkg/");
    if (pkg) {
        pkg += 5;
        const char* end = strchr(pkg, '/');
        if (end) return String(pkg, (int)(end - pkg));
        return String(pkg);
    }
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    String b = base;
    int dot = b.rfind('.');
    if (dot > 0) b = b.substr(0, dot);
    if (b.empty()) b = "app";
    return b;
}

bool ncode_run_source(NCOut* out, const char* src, int srclen,
                      const char* path, const char** args, int argc) {
    NCtx* ctx = new NCtx();
    ctx->out = out;
    ctx->sandbox = n_script_sandbox_name(path);
    for (int i = 0; i < argc && i < 16; i++) ctx->args.push(args[i] ? args[i] : "");

    List<NTok> toks;
    String lex_err;
    if (!n_lex(src, srclen, toks, lex_err)) {
        ctx_pln(ctx, "ncode: lex error: ");
        ctx_pln(ctx, lex_err.c_str());
        delete ctx;
        return false;
    }

    NParser p(ctx);
    p.toks = toks;
    NNode* prog = node_new(K_PROG, 0);
    bool ok = p.parse_program(prog);
    if (ok && !ctx->err) {
        NEnv* g = env_new(0);
        ctx->env = g;
        ok = n_run(ctx, prog);
        // note: env/prog/ctx are freed below via the !keep branch (or kept
        // intentionally when a live start_r_os() window references them)
    }
    bool keep = ctx->os != 0;
    bool result = ok && !ctx->err;
    if (!keep) {
        if (ctx->env) delete ctx->env;
        node_free(prog);
        delete ctx;
    } else {
        // The live start_r_os() window owns ctx/env/prog and outlives the
        // caller, so it must not reference the caller's (possibly stack)
        // NCOut after we return. Hand ctx its own heap copy of the sink so a
        // later print doesn't read a dangling function pointer (crash to a
        // garbage address like 0x100000000).
        if (out) {
            NCOut* owned = new NCOut();
            *owned = *out;
            ctx->out = owned;
        }
    }
    // if keep: the live start_r_os() window owns ctx/env/prog (intentional keep)
    return result;
}

bool ncode_run_file(NCOut* out, FSNode* f, const char** args, int argc) {
    if (!f || f->is_dir || f->size == 0) {
        if (out) out->pln("ncode: empty or missing file");
        return false;
    }
    return ncode_run_source(out, (const char*)f->data, (int)f->size,
                            f->name.c_str(), args, argc);
}

// =============================================================================
//  self test (interpreter core only; no GUI)
// =============================================================================
struct NTestOut {
    String buf;
    static void cb(void* ud, const char* s) { ((NTestOut*)ud)->buf += s; }
    NCOut out() { NCOut o; o.print = &NTestOut::cb; o.ud = this; return o; }
};

static void n_test_run(const char* src, const char** args, int argc, String& outbuf, bool* okout) {
    NTestOut to;
    NCOut sink = to.out();    bool ok = ncode_run_source(&sink, src, (int)nefu::strlen(src), "selftest.ncode", args, argc);
    outbuf = to.buf;
    *okout = ok;
}

int ncode_self_test() {
    int fails = 0;

    {
        String out; bool ok;
        n_test_run("#nefu\nimport<std>\nvar x = 3;\nmain() { var y = x * 2 + 1; println(y); }\n",
                   0, 0, out, &ok);
        if (!ok || out.find("7") < 0) fails++;
    }
    {
        String out; bool ok;
        n_test_run(
            "import<std>\n"
            "main() {\n"
            "  var s = 0;\n"
            "  for (var i = 0; i < 10; i++) {\n"
            "    if (i == 3) continue;\n"
            "    if (i == 7) break;\n"
            "    s = s + i;\n"
            "  }\n"
            "  while (s > 25) { s = s - 1; }\n"
            "  if (s != 18) { println(\"bad\"); } else { println(\"s=\" + s); }\n"
            "}\n",
            0, 0, out, &ok);
        if (!ok || out.find("s=18") < 0) fails++;
    }
    {
        String out; bool ok;
        n_test_run(
            "import<std>\n"
            "func fib(n) { if (n < 2) return n; return fib(n-1) + fib(n-2); }\n"
            "main() { println(fib(10)); }\n",
            0, 0, out, &ok);
        if (!ok || out.find("55") < 0) fails++;
    }
    {
        String out; bool ok;
        n_test_run(
            "import<std>\n"
            "main() {\n"
            "  var s = \"Hello, nefu!\";\n"
            "  println(s.len);\n"
            "  println(s.upper());\n"
            "  println(s.substr(0, 5));\n"
            "  var a = [1, 2, 3];\n"
            "  a.push(4);\n"
            "  println(a.len);\n"
            "  println(a[0]);\n"
            "  println(a.join(\"-\"));\n"
            "}\n",
            0, 0, out, &ok);
        if (!ok) fails++;
        else {
            if (out.find("12") < 0) fails++;
            if (out.find("HELLO, NEFU!") < 0) fails++;
            if (out.find("Hello") < 0) fails++;
            if (out.find("1-2-3-4") < 0) fails++;
        }
    }
    {
        String out; bool ok;
        n_test_run(
            "import<std>\nmain() { println(file_exists(\"/etc\")); }\n",
            0, 0, out, &ok);
        if (ok || out.find("unknown function") < 0) fails++;
    }
    {
        String out; bool ok;
        const char* args[] = {"alpha", "beta"};
        n_test_run(
            "import<std>\nmain() { var a = args(); println(a.len); println(a[1]); }\n",
            args, 2, out, &ok);
        if (!ok || out.find("2") < 0 || out.find("beta") < 0) fails++;
    }
    {
        String out; bool ok;
        n_test_run(
            "import<std>\nmain() { var x = 5; var f = 2.5; println(x + f); println(str(x) + \"!\"); }\n",
            0, 0, out, &ok);
        if (!ok) fails++;
        else {
            if (out.find("7.5") < 0) fails++;
            if (out.find("5!") < 0) fails++;
        }
    }
    {
        String out; bool ok;
        n_test_run("import<std>\nvar q = 1;\n", 0, 0, out, &ok);
        if (ok || out.find("main") < 0) fails++;
    }
    return fails;
}

} // namespace nefu
