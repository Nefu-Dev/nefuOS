// php2html.h — PHP template mini-transpiler for nefuOS
//
// Executes common PHP constructs at template time and emits the resulting
// HTML, so .php pages render without a server-side PHP runtime. The emitted
// HTML is then fed into the normal nefuOS HTML pipeline; <script> inside it
// runs through the existing mini JS engine.
//
// Supported language surface (templates, not a full PHP):
//   <?php ... ?> / <?= expr ?> / <? ... ?> blocks
//   $var assignment, string concat '.', arithmetic + - * / %, unary - ! 
//   comparisons == != < > <= >=, logic && ||, ternary ?:
//   if / elseif / else, for(;;), foreach($a as $v) / ($a as $k=>$v), while
//   function name($a,$b) {... return ...;}  (own scope, shallow depth)
//   arrays: array(...), $a[] = v, $a[k] = v, $a[k], count/foreach/implode
//   double-quoted strings with "$var" interpolation + escapes
//   builtins: strlen strtoupper strtolower ucfirst ucwords substr trim ltrim
//     rtrim str_replace strpos count sizeof implode join explode isset empty
//     intval floatval number_format abs min max round floor ceil date
//     htmlspecialchars str_repeat substr_count str_pad
//
// On any parse/runtime error the failing block emits nothing, the error text
// is written to the debug log (klogf) only — never to the page — and the
// rest of the document continues (non-fatal, matching the browser policy).

#ifndef NEFU_PHP2HTML_H
#define NEFU_PHP2HTML_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace nfuphp {

// ---------------------------------------------------------------------------
// dynamic values (strings; numbers parsed on demand; arrays use a custom
// compact encoding: '{' k ':' v ';' ... '}' with : ; \ escaped)
// ---------------------------------------------------------------------------
struct PVal {
    const char* s;
    int len;
    PVal() : s(""), len(0) {}
    PVal(const char* t) : s(t ? t : ""), len(t ? (int)strlen(t) : 0) {}
};

static bool pv_num(const PVal& v, double* out) {
    if (!v.s || !v.s[0]) { *out = 0.0; return true; }
    char* end = 0;
    double d = strtod(v.s, &end);
    if (end && *end == 0) { *out = d; return true; }
    // also accept leading-space / "1.5abc" like PHP's loose cast
    *out = strtod(v.s, 0);
    return true;
}
static bool pv_truthy(const PVal& v) {
    if (!v.s) return false;
    if (v.s[0] == 0) return false;
    if (v.s[0] == '0' && v.s[1] == 0) return false;
    return true;
}

// ---------------------------------------------------------------------------
// tokenizer
// ---------------------------------------------------------------------------
enum PTokT {
    PT_EOF, PT_ID, PT_VAR, PT_NUM, PT_STR, PT_OP, PT_LP, PT_RP, PT_LB, PT_RB,
    PT_LBR, PT_RBR, PT_SEMI, PT_COMMA, PT_COLON, PT_QMARK, PT_FATARROW
};

struct PTok {
    int type;
    char text[64];      // id name (no $), string literal contents (unescaped),
                        // number text, or operator text (<=1-3 chars)
};

struct PLex {
    const char* p;
    PTok cur;
    bool fail;
    int line;
};

static void lex_next(PLex* L) {
    const char* p = L->p;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (*p == '\n') L->line++;
            p++;
        }
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '#' && p[1] != '{') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') L->line++; p++; }
            if (*p) p += 2;
            continue;
        }
        break;
    }
    L->p = p;
    if (!*p) { L->cur.type = PT_EOF; L->cur.text[0] = 0; return; }
    // identifiers / keywords
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
        int n = 0;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_') {
            if (n < 62) L->cur.text[n++] = *p;
            p++;
        }
        L->cur.text[n] = 0;
        L->cur.type = PT_ID;
        L->p = p;
        return;
    }
    // $var
    if (*p == '$') {
        p++;
        int n = 0;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') {
                if (n < 62) L->cur.text[n++] = *p;
                p++;
            }
            L->cur.text[n] = 0;
            L->cur.type = PT_VAR;
            L->p = p;
            return;
        }
        // invalid variable like $$x — emit a bare VAR; the parser will fail on it
        if (n == 0) {
            L->cur.type = PT_VAR;
            L->cur.text[0] = 0;
            L->p = p;
            return;
        }
    }
    // strings
    if (*p == '"' || *p == '\'') {
        char q = *p;
        p++;
        int n = 0;
        while (*p && *p != q) {
            if (*p == '\\' && p[1]) {
                p++;
                char c = *p;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
                if (n < 62) L->cur.text[n++] = c;
                p++;
                continue;
            }
            if (n < 62) L->cur.text[n++] = *p;
            p++;
        }
        if (*p == q) p++;
        L->cur.text[n] = 0;
        L->cur.type = PT_STR;
        L->p = p;
        return;
    }
    // numbers
    if ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
        int n = 0;
        while ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' ||
               ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E'))) {
            if (n < 62) L->cur.text[n++] = *p;
            p++;
        }
        L->cur.text[n] = 0;
        L->cur.type = PT_NUM;
        L->p = p;
        return;
    }
    // punctuation / operators
    struct { const char* t; int ty; } tw[] = {
        {"=>", PT_FATARROW}, {"<=", PT_OP}, {">=", PT_OP}, {"==", PT_OP},
        {"!=", PT_OP}, {"&&", PT_OP}, {"||", PT_OP}, {"++", PT_OP}, {"--", PT_OP},
        {"+=", PT_OP}, {"-=", PT_OP}, {"*=", PT_OP}, {"/=", PT_OP}, {".=", PT_OP},
        {"(", PT_LP}, {")", PT_RP}, {"[", PT_LB}, {"]", PT_RB},
        {"{", PT_LBR}, {"}", PT_RBR}, {";", PT_SEMI}, {",", PT_COMMA},
        {":", PT_COLON}, {"?", PT_QMARK}, {0, PT_EOF}
    };
    for (int i = 0; tw[i].t; i++) {
        int tl = (int)strlen(tw[i].t);
        if (strncmp(p, tw[i].t, (size_t)tl) == 0) {
            strncpy(L->cur.text, tw[i].t, 3);
            L->cur.text[3] = 0;
            L->cur.type = tw[i].ty;
            L->p = p + tl;
            return;
        }
    }
    // single-char ops: + - * / % ! < > . = &
    if (strchr("+-*/%!<>=.&", *p)) {
        L->cur.text[0] = *p;
        L->cur.text[1] = 0;
        L->cur.type = PT_OP;
        L->p = p + 1;
        return;
    }
    L->fail = true;
    L->cur.type = PT_EOF;
    L->cur.text[0] = 0;
}

static void lex_init(PLex* L, const char* src) {
    L->p = src;
    L->fail = false;
    L->line = 1;
    lex_next(L);
}
static void lex_adv(PLex* L) { lex_next(L); }
static bool lex_is(PLex* L, int t) { return L->cur.type == t; }
static bool lex_isop(PLex* L, const char* op) {
    return L->cur.type == PT_OP && strcmp(L->cur.text, op) == 0;
}
static bool lex_isid(PLex* L, const char* id) {
    return L->cur.type == PT_ID && strcmp(L->cur.text, id) == 0;
}

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------
enum PEKind {
    PE_LIT, PE_VAR, PE_BIN, PE_UN, PE_CALL, PE_ARR, PE_IDX, PE_TERN
};
struct PExpr {
    int kind;
    PVal lit;           // PE_LIT literal value (string or number text)
    char* lit_own;      // owned copy of the literal text (stable across tokens)
    char name[64];      // PE_VAR var name / PE_CALL func name / PE_IDX base var name
    int op;             // PE_BIN: 0='+' 1='-' 2='*' 3='/' 4='%' 5='.' 6='==' 7='!=' 8='<' 9='>' 10='<=' 11='>=' 12='&&' 13='||'
                        // PE_UN: 0='-' 1='!' 2='++' 3='--'
    PExpr* a;
    PExpr* b;
    PExpr* c;
    PExpr** args; int nargs;
    // PE_IDX: name is base var, a is index expr
    // PE_ARR: args are elements; each element may be [key, val] pair (args in pairs)
};

enum PSKind {
    PS_ASSIGN, PS_ASSIGNOP, PS_INCDEC, PS_ECHO, PS_IF, PS_FOR, PS_FOREACH,
    PS_WHILE, PS_RETURN, PS_EXPR, PS_FUNC, PS_ARRSET, PS_BREAK, PS_NOP
};
struct PStmt {
    int kind;
    char name[64];      // var / func name
    PExpr* e;           // rhs / cond / expr / return expr
    int op;             // assign-op id (for PS_ASSIGNOP), ++/-- id
    PStmt** body; int nbody;   // block (if/for/while/foreach/func body)
    PStmt* els;         // else / elseif chain (PS_IF)
    PExpr* c;           // PS_ARRSET value expr
    // PS_FOR
    PStmt** init; int ninit;
    PExpr* step;
    // PS_FOREACH: name = value var; e = array expr; kv = key var (may be "")
    char kv[64];
};

struct PProg {
    PStmt** stmts; int nstmts;
};

// function table (built as the source is parsed; forward calls resolved at run)
struct PFunc {
    char name[64];
    char params[8][64];
    int nparams;
    PStmt** body; int nbody;
    PFunc* next;
};

// ---------------------------------------------------------------------------
// parser (recursive descent)
// ---------------------------------------------------------------------------
struct PParser {
    PLex* L;
    PFunc** fns;
    bool fail;
};

static PExpr* pe_new(int kind) {
    PExpr* e = (PExpr*)malloc(sizeof(PExpr));
    memset(e, 0, sizeof(PExpr));
    e->kind = kind;
    return e;
}
static void pe_free(PExpr* e) {
    if (!e) return;
    if (e->lit_own) { free(e->lit_own); e->lit_own = 0; }
    pe_free(e->a); pe_free(e->b); pe_free(e->c);
    for (int i = 0; i < e->nargs; i++) pe_free(e->args[i]);
    if (e->args) free(e->args);
    free(e);
}
static void ps_free(PStmt* st) {
    if (!st) return;
    pe_free(st->e);
    pe_free(st->step);
    for (int i = 0; i < st->nbody; i++) ps_free(st->body[i]);
    if (st->body) free(st->body);
    for (int i = 0; i < st->ninit; i++) ps_free(st->init[i]);
    if (st->init) free(st->init);
    ps_free(st->els);
    free(st);
}
static void ps_array_free(PStmt** arr, int n) {
    for (int i = 0; i < n; i++) ps_free(arr[i]);
    if (arr) free(arr);
}

// forward decls
static PExpr* pp_expr(PParser* P);
static PExpr* pp_ternary(PParser* P);
static PExpr* pp_or(PParser* P);
static PExpr* pp_and(PParser* P);
static PExpr* pp_cmp(PParser* P);
static PExpr* pp_add(PParser* P);
static PExpr* pp_mul(PParser* P);
static PExpr* pp_un(PParser* P);
static PExpr* pp_postfix(PParser* P);
static PExpr* pp_atom(PParser* P);

static PExpr* pp_atom(PParser* P) {
    PLex* L = P->L;
    if (lex_is(L, PT_NUM)) {
        PExpr* e = pe_new(PE_LIT);
        int ln = (int)strlen(L->cur.text);
        e->lit_own = (char*)malloc((size_t)ln + 1);
        memcpy(e->lit_own, L->cur.text, (size_t)ln);
        e->lit_own[ln] = 0;
        e->lit = PVal(e->lit_own);
        lex_adv(L);
        return e;
    }
    if (lex_is(L, PT_STR)) {
        PExpr* e = pe_new(PE_LIT);
        int ln = (int)strlen(L->cur.text);
        e->lit_own = (char*)malloc((size_t)ln + 1);
        memcpy(e->lit_own, L->cur.text, (size_t)ln);
        e->lit_own[ln] = 0;
        e->lit = PVal(e->lit_own);
        lex_adv(L);
        return e;
    }
    if (lex_is(L, PT_VAR)) {
        PExpr* e = pe_new(PE_VAR);
        strncpy(e->name, L->cur.text, 63);
        lex_adv(L);
        return e;
    }
    if (lex_is(L, PT_ID)) {
        char nm[64];
        strncpy(nm, L->cur.text, 63);
        lex_adv(L);
        // array(...) is a literal, not a call — it may carry key => value pairs
        // and must not go down the PE_CALL argument path (which would choke on '=>')
        if (lex_is(L, PT_LP) && strcmp(nm, "array") == 0) {
            PExpr* e = pe_new(PE_ARR);
            lex_adv(L);
            e->args = 0; e->nargs = 0;
            int cap = 4;
            e->args = (PExpr**)malloc(sizeof(PExpr*) * (size_t)cap);
            if (!lex_is(L, PT_RP)) {
                for (;;) {
                    if (e->nargs + 2 > cap) {
                        cap *= 2;
                        e->args = (PExpr**)realloc(e->args, sizeof(PExpr*) * (size_t)cap);
                    }
                    PExpr* v1 = pp_ternary(P);
                    if (lex_is(L, PT_FATARROW)) {
                        lex_adv(L);
                        PExpr* v2 = pp_ternary(P);
                        v1->name[0] = 'K';
                        e->args[e->nargs++] = v1;
                        e->args[e->nargs++] = v2;
                    } else {
                        e->args[e->nargs++] = v1;
                    }
                    if (lex_is(L, PT_COMMA)) { lex_adv(L); continue; }
                    break;
                }
            }
            if (!lex_is(L, PT_RP)) { P->fail = true; }
            else lex_adv(L);
            return e;
        }
        if (lex_is(L, PT_LP)) {
            // function call
            PExpr* e = pe_new(PE_CALL);
            strncpy(e->name, nm, 63);
            lex_adv(L);
            e->args = 0; e->nargs = 0;
            int cap = 4;
            e->args = (PExpr**)malloc(sizeof(PExpr*) * (size_t)cap);
            if (!lex_is(L, PT_RP)) {
                for (;;) {
                    if (e->nargs >= cap) {
                        cap *= 2;
                        e->args = (PExpr**)realloc(e->args, sizeof(PExpr*) * (size_t)cap);
                    }
                    e->args[e->nargs++] = pp_ternary(P);
                    if (lex_is(L, PT_COMMA)) { lex_adv(L); continue; }
                    break;
                }
            }
            if (!lex_is(L, PT_RP)) { P->fail = true; }
            else lex_adv(L);
            return e;
        }
        // keyword: true / false / null
        if (strcmp(nm, "true") == 0) { PExpr* e = pe_new(PE_LIT); e->lit = PVal("1"); return e; }
        if (strcmp(nm, "false") == 0) { PExpr* e = pe_new(PE_LIT); e->lit = PVal(""); return e; }
        if (strcmp(nm, "null") == 0) { PExpr* e = pe_new(PE_LIT); e->lit = PVal(""); return e; }
        // array(...)
        if (strcmp(nm, "array") == 0 && lex_is(L, PT_LP)) {
            PExpr* e = pe_new(PE_ARR);
            lex_adv(L);
            e->args = 0; e->nargs = 0;
            int cap = 4;
            e->args = (PExpr**)malloc(sizeof(PExpr*) * (size_t)cap);
            if (!lex_is(L, PT_RP)) {
                for (;;) {
                    if (e->nargs >= cap) {
                        cap *= 2;
                        e->args = (PExpr**)realloc(e->args, sizeof(PExpr*) * (size_t)cap);
                    }
                    // [key =>] value : key/value are stored as PAIRS (2 exprs)
                    PExpr* v1 = pp_ternary(P);
                    if (lex_is(L, PT_FATARROW)) {
                        lex_adv(L);
                        PExpr* v2 = pp_ternary(P);
                        v1->name[0] = 'K';   // pair-key marker (consumes 2 args at runtime)
                        if (e->nargs + 2 > cap) {
                            cap *= 2;
                            e->args = (PExpr**)realloc(e->args, sizeof(PExpr*) * (size_t)cap);
                        }
                        e->args[e->nargs++] = v1;   // key
                        e->args[e->nargs++] = v2;   // val
                    } else {
                        e->args[e->nargs++] = v1;
                    }
                    if (lex_is(L, PT_COMMA)) { lex_adv(L); continue; }
                    break;
                }
            }
            if (!lex_is(L, PT_RP)) { P->fail = true; }
            else lex_adv(L);
            return e;
        }
        P->fail = true;
        return pe_new(PE_LIT);
    }
    if (lex_is(L, PT_LP)) {
        lex_adv(L);
        PExpr* e = pp_ternary(P);
        if (!lex_is(L, PT_RP)) P->fail = true;
        else lex_adv(L);
        return e;
    }
    P->fail = true;
    return pe_new(PE_LIT);
}

static PExpr* pp_postfix(PParser* P) {
    PLex* L = P->L;
    PExpr* e = pp_atom(P);
    for (;;) {
        if (lex_is(L, PT_LB)) {
            PExpr* idx = pe_new(PE_IDX);
            if (e->kind == PE_VAR) strncpy(idx->name, e->name, 63);
            else strncpy(idx->name, e->name, 63);
            pe_free(e);
            lex_adv(L);
            idx->a = pp_ternary(P);
            if (!lex_is(L, PT_RB)) P->fail = true;
            else lex_adv(L);
            e = idx;
            continue;
        }
        if (lex_isop(L, "++") || lex_isop(L, "--")) {
            PExpr* u = pe_new(PE_UN);
            u->op = lex_isop(L, "++") ? 2 : 3;
            u->a = e;
            lex_adv(L);
            e = u;
            continue;
        }
        break;
    }
    return e;
}

static PExpr* pp_un(PParser* P) {
    PLex* L = P->L;
    if (lex_isop(L, "-")) {
        lex_adv(L);
        PExpr* e = pe_new(PE_UN);
        e->op = 0;
        e->a = pp_un(P);
        return e;
    }
    if (lex_isop(L, "!")) {
        lex_adv(L);
        PExpr* e = pe_new(PE_UN);
        e->op = 1;
        e->a = pp_un(P);
        return e;
    }
    return pp_postfix(P);
}

static PExpr* pp_mul(PParser* P) {
    PLex* L = P->L;
    PExpr* a = pp_un(P);
    for (;;) {
        int op = -1;
        if (lex_isop(L, "*")) op = 2;
        else if (lex_isop(L, "/")) op = 3;
        else if (lex_isop(L, "%")) op = 4;
        if (op < 0) return a;
        lex_adv(L);
        PExpr* e = pe_new(PE_BIN);
        e->op = op; e->a = a; e->b = pp_un(P);
        a = e;
    }
}

static PExpr* pp_add(PParser* P) {
    PLex* L = P->L;
    PExpr* a = pp_mul(P);
    for (;;) {
        int op = -1;
        if (lex_isop(L, "+")) op = 0;
        else if (lex_isop(L, "-")) op = 1;
        else if (lex_isop(L, ".")) op = 5;
        if (op < 0) return a;
        lex_adv(L);
        PExpr* e = pe_new(PE_BIN);
        e->op = op; e->a = a; e->b = pp_mul(P);
        a = e;
    }
}

static PExpr* pp_cmp(PParser* P) {
    PLex* L = P->L;
    PExpr* a = pp_add(P);
    for (;;) {
        int op = -1;
        if (lex_isop(L, "==")) op = 6;
        else if (lex_isop(L, "!=")) op = 7;
        else if (lex_isop(L, "<")) op = 8;
        else if (lex_isop(L, ">")) op = 9;
        else if (lex_isop(L, "<=")) op = 10;
        else if (lex_isop(L, ">=")) op = 11;
        if (op < 0) return a;
        lex_adv(L);
        PExpr* e = pe_new(PE_BIN);
        e->op = op; e->a = a; e->b = pp_add(P);
        a = e;
    }
}

static PExpr* pp_and(PParser* P) {
    PLex* L = P->L;
    PExpr* a = pp_cmp(P);
    while (lex_isop(L, "&&")) {
        lex_adv(L);
        PExpr* e = pe_new(PE_BIN);
        e->op = 12; e->a = a; e->b = pp_cmp(P);
        a = e;
    }
    return a;
}

static PExpr* pp_or(PParser* P) {
    PLex* L = P->L;
    PExpr* a = pp_and(P);
    while (lex_isop(L, "||")) {
        lex_adv(L);
        PExpr* e = pe_new(PE_BIN);
        e->op = 13; e->a = a; e->b = pp_and(P);
        a = e;
    }
    return a;
}

static PExpr* pp_ternary(PParser* P) {
    PLex* L = P->L;
    PExpr* c = pp_or(P);
    if (lex_is(L, PT_QMARK)) {
        lex_adv(L);
        PExpr* e = pe_new(PE_TERN);
        e->a = c;
        e->b = pp_ternary(P);
        if (lex_is(L, PT_COLON)) {
            lex_adv(L);
            e->c = pp_ternary(P);
        } else {
            P->fail = true;
        }
        return e;
    }
    return c;
}

static PExpr* pp_expr(PParser* P) { return pp_ternary(P); }
static PStmt* pp_stmt(PParser* P);  // fwd

// statement-level helpers
static bool pp_block(PParser* P, PStmt*** out, int* n) {
    PLex* L = P->L;
    *out = 0; *n = 0;
    int cap = 4;
    PStmt** arr = (PStmt**)malloc(sizeof(PStmt*) * (size_t)cap);
    if (!lex_is(L, PT_LBR)) return false;
    lex_adv(L);
    while (!lex_is(L, PT_RBR) && !lex_is(L, PT_EOF)) {
        PStmt* st = pp_stmt(P);
        if (!st) break;
        if (*n >= cap) {
            cap *= 2;
            arr = (PStmt**)realloc(arr, sizeof(PStmt*) * (size_t)cap);
        }
        arr[(*n)++] = st;
        if (P->fail) break;
    }
    if (!lex_is(L, PT_RBR)) P->fail = true;
    else lex_adv(L);
    *out = arr;
    return true;
}

static PStmt* pp_func(PParser* P, PFunc* fn) {
    // after 'function name' — parse (params) { body }
    PLex* L = P->L;
    fn->nparams = 0;
    if (!lex_is(L, PT_LP)) { P->fail = true; return 0; }
    lex_adv(L);
    while (!lex_is(L, PT_RP) && !lex_is(L, PT_EOF)) {
        if (lex_is(L, PT_VAR)) {
            if (fn->nparams < 8) strncpy(fn->params[fn->nparams++], L->cur.text, 63);
            lex_adv(L);
        }
        if (lex_is(L, PT_COMMA)) lex_adv(L);
        else break;
    }
    if (!lex_is(L, PT_RP)) { P->fail = true; return 0; }
    lex_adv(L);
    PStmt* st = (PStmt*)malloc(sizeof(PStmt));
    memset(st, 0, sizeof(PStmt));
    st->kind = PS_FUNC;
    strncpy(st->name, fn->name, 63);
    pp_block(P, &st->body, &st->nbody);
    return st;
}

static PStmt* pp_stmt(PParser* P) {
    PLex* L = P->L;
    if (lex_is(L, PT_EOF)) return 0;
    // function definition
    if (lex_isid(L, "function")) {
        lex_adv(L);
        if (lex_is(L, PT_ID)) {
            PFunc* fn = (PFunc*)malloc(sizeof(PFunc));
            memset(fn, 0, sizeof(PFunc));
            strncpy(fn->name, L->cur.text, 63);
            lex_adv(L);
            PStmt* st = pp_func(P, fn);
            if (st) {
                fn->body = st->body;
                fn->nbody = st->nbody;
                st->body = 0;      // ownership moved to the function table
                st->nbody = 0;     // so ps_free of the placeholder is safe
                fn->next = *P->fns;
                *P->fns = fn;
                return st;         // PS_FUNC placeholder, freed with the program
            }
            free(fn);
            return 0;
        }
        P->fail = true;
        return 0;
    }
    // echo / print
    if (lex_isid(L, "echo") || lex_isid(L, "print")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_ECHO;
        // echo a, b, c...
        int cap = 2;
        st->e = 0;
        PExpr** arr = 0;
        int na = 0;
        arr = (PExpr**)malloc(sizeof(PExpr*) * (size_t)cap);
        for (;;) {
            if (na >= cap) { cap *= 2; arr = (PExpr**)realloc(arr, sizeof(PExpr*) * (size_t)cap); }
            arr[na++] = pp_expr(P);
            if (lex_is(L, PT_COMMA)) { lex_adv(L); continue; }
            break;
        }
        // pack multiple echoes via PE_ARR semantics: store as expr with args
        PExpr* e = pe_new(PE_ARR);
        e->args = arr; e->nargs = na;
        st->e = e;
        if (!lex_is(L, PT_SEMI)) { /* allow } or ?> */ }
        else lex_adv(L);
        return st;
    }
    // if
    if (lex_isid(L, "if")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_IF;
        if (!lex_is(L, PT_LP)) { P->fail = true; return st; }
        lex_adv(L);
        st->e = pp_expr(P);
        if (!lex_is(L, PT_RP)) P->fail = true;
        else lex_adv(L);
        pp_block(P, &st->body, &st->nbody);
        // elseif / else
        if (lex_isid(L, "elseif")) {
            lex_adv(L);
            PStmt* el = (PStmt*)malloc(sizeof(PStmt));
            memset(el, 0, sizeof(PStmt));
            el->kind = PS_IF;
            if (!lex_is(L, PT_LP)) P->fail = true;
            else lex_adv(L);
            el->e = pp_expr(P);
            if (!lex_is(L, PT_RP)) P->fail = true;
            else lex_adv(L);
            pp_block(P, &el->body, &el->nbody);
            st->els = el;
        } else if (lex_isid(L, "else")) {
            lex_adv(L);
            PStmt* el = (PStmt*)malloc(sizeof(PStmt));
            memset(el, 0, sizeof(PStmt));
            el->kind = PS_NOP;
            pp_block(P, &el->body, &el->nbody);
            st->els = el;
        }
        return st;
    }
    // for
    if (lex_isid(L, "for")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_FOR;
        if (!lex_is(L, PT_LP)) { P->fail = true; return st; }
        lex_adv(L);
        // init statements (assignments / comma)
        int cap = 2;
        st->init = (PStmt**)malloc(sizeof(PStmt*) * (size_t)cap);
        st->ninit = 0;
        while (!lex_is(L, PT_SEMI) && !lex_is(L, PT_EOF)) {
            if (lex_is(L, PT_VAR)) {
                PStmt* as = (PStmt*)malloc(sizeof(PStmt));
                memset(as, 0, sizeof(PStmt));
                strncpy(as->name, L->cur.text, 63);
                lex_adv(L);
                if (lex_isop(L, "=")) {
                    lex_adv(L);
                    as->kind = PS_ASSIGN;
                    as->e = pp_expr(P);
                } else if (lex_isop(L, "++") || lex_isop(L, "--")) {
                    as->kind = PS_INCDEC;
                    as->op = lex_isop(L, "++") ? 2 : 3;
                    lex_adv(L);
                }
                if (st->ninit >= cap) { cap *= 2; st->init = (PStmt**)realloc(st->init, sizeof(PStmt*) * (size_t)cap); }
                st->init[st->ninit++] = as;
                if (lex_is(L, PT_COMMA)) { lex_adv(L); continue; }
            }
            break;
        }
        if (!lex_is(L, PT_SEMI)) P->fail = true;
        else lex_adv(L);
        if (!lex_is(L, PT_SEMI)) st->e = pp_expr(P);
        if (!lex_is(L, PT_SEMI)) P->fail = true;
        else lex_adv(L);
        if (!lex_is(L, PT_RP)) st->step = pp_expr(P);
        if (!lex_is(L, PT_RP)) P->fail = true;
        else lex_adv(L);
        // single statement or block
        if (lex_is(L, PT_LBR)) pp_block(P, &st->body, &st->nbody);
        else {
            st->body = (PStmt**)malloc(sizeof(PStmt*));
            st->body[0] = pp_stmt(P);
            st->nbody = st->body[0] ? 1 : 0;
        }
        return st;
    }
    // foreach
    if (lex_isid(L, "foreach")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_FOREACH;
        if (!lex_is(L, PT_LP)) { P->fail = true; return st; }
        lex_adv(L);
        st->e = pp_expr(P);
        if (!lex_isid(L, "as")) { P->fail = true; return st; }
        lex_adv(L);
        if (lex_is(L, PT_VAR)) {
            strncpy(st->kv, L->cur.text, 63);
            lex_adv(L);
            if (lex_is(L, PT_FATARROW)) {
                lex_adv(L);
                if (lex_is(L, PT_VAR)) { strncpy(st->name, L->cur.text, 63); lex_adv(L); }
                else P->fail = true;
            } else {
                strncpy(st->name, st->kv, 63);
                st->kv[0] = 0;
            }
        } else P->fail = true;
        if (!lex_is(L, PT_RP)) P->fail = true;
        else lex_adv(L);
        if (lex_is(L, PT_LBR)) pp_block(P, &st->body, &st->nbody);
        else {
            st->body = (PStmt**)malloc(sizeof(PStmt*));
            st->body[0] = pp_stmt(P);
            st->nbody = st->body[0] ? 1 : 0;
        }
        return st;
    }
    // while
    if (lex_isid(L, "while")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_WHILE;
        if (!lex_is(L, PT_LP)) { P->fail = true; return st; }
        lex_adv(L);
        st->e = pp_expr(P);
        if (!lex_is(L, PT_RP)) P->fail = true;
        else lex_adv(L);
        if (lex_is(L, PT_LBR)) pp_block(P, &st->body, &st->nbody);
        else {
            st->body = (PStmt**)malloc(sizeof(PStmt*));
            st->body[0] = pp_stmt(P);
            st->nbody = st->body[0] ? 1 : 0;
        }
        return st;
    }
    // return
    if (lex_isid(L, "return")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_RETURN;
        if (!lex_is(L, PT_SEMI) && !lex_is(L, PT_RBR) && !lex_is(L, PT_EOF))
            st->e = pp_expr(P);
        if (lex_is(L, PT_SEMI)) lex_adv(L);
        return st;
    }
    // break
    if (lex_isid(L, "break")) {
        lex_adv(L);
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_BREAK;
        if (lex_is(L, PT_SEMI)) lex_adv(L);
        return st;
    }
    // assignment: $x = expr | $x .= expr | $x += ... | $x++ | $x[] = v | $x[k] = v
    if (lex_is(L, PT_VAR)) {
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        strncpy(st->name, L->cur.text, 63);
        lex_adv(L);
        if (lex_isop(L, "=")) {
            lex_adv(L);
            st->kind = PS_ASSIGN;
            st->e = pp_expr(P);
        } else if (lex_isop(L, ".=")) {
            lex_adv(L);
            st->kind = PS_ASSIGNOP; st->op = 5;
            st->e = pp_expr(P);
        } else if (lex_isop(L, "+=")) {
            lex_adv(L);
            st->kind = PS_ASSIGNOP; st->op = 0;
            st->e = pp_expr(P);
        } else if (lex_isop(L, "-=")) {
            lex_adv(L);
            st->kind = PS_ASSIGNOP; st->op = 1;
            st->e = pp_expr(P);
        } else if (lex_isop(L, "*=")) {
            lex_adv(L);
            st->kind = PS_ASSIGNOP; st->op = 2;
            st->e = pp_expr(P);
        } else if (lex_isop(L, "/=")) {
            lex_adv(L);
            st->kind = PS_ASSIGNOP; st->op = 3;
            st->e = pp_expr(P);
        } else if (lex_isop(L, "++")) {
            lex_adv(L);
            st->kind = PS_INCDEC; st->op = 2;
        } else if (lex_isop(L, "--")) {
            lex_adv(L);
            st->kind = PS_INCDEC; st->op = 3;
        } else if (lex_is(L, PT_LB)) {
            lex_adv(L);
            st->kind = PS_ARRSET;
            // empty index is legal: $a[] = v appends
            if (!lex_is(L, PT_RB)) st->e = pp_expr(P);
            if (!lex_is(L, PT_RB)) P->fail = true;
            else lex_adv(L);
            if (lex_isop(L, "=")) {
                lex_adv(L);
                st->op = 0;
                st->c = pp_expr(P);
            } else if (lex_isop(L, ".=")) {
                lex_adv(L);
                st->op = 5;
                st->c = pp_expr(P);
            } else {
                P->fail = true;
            }
        } else {
            P->fail = true;
        }
        if (lex_is(L, PT_SEMI)) lex_adv(L);
        return st;
    }
    // bare expression (function call as statement)
    PExpr* e = pp_expr(P);
    if (e) {
        PStmt* st = (PStmt*)malloc(sizeof(PStmt));
        memset(st, 0, sizeof(PStmt));
        st->kind = PS_EXPR;
        st->e = e;
        if (lex_is(L, PT_SEMI)) lex_adv(L);
        return st;
    }
    return 0;
}

static bool pp_parse(PParser* P, PProg* prog) {
    prog->stmts = 0; prog->nstmts = 0;
    int cap = 8;
    PStmt** arr = (PStmt**)malloc(sizeof(PStmt*) * (size_t)cap);
    while (!lex_is(P->L, PT_EOF) && !P->fail) {
        PStmt* st = pp_stmt(P);
        if (!st) break;
        if (prog->nstmts >= cap) {
            cap *= 2;
            arr = (PStmt**)realloc(arr, sizeof(PStmt*) * (size_t)cap);
        }
        arr[prog->nstmts++] = st;
    }
    prog->stmts = arr;
    return !P->fail;
}

// ---------------------------------------------------------------------------
// VM
// ---------------------------------------------------------------------------
struct PVm {
    PVal vars[96];
    char vnames[96][64];
    int nvars;
    PFunc** fns;   // pointer to the shared function-list head
    char* out; int outn; int outcap;
    // stable string pool: transient evaluation buffers are copied here so a
    // later sibling evaluation (which reuses the same scratch) cannot corrupt
    // an earlier result that the caller still holds.
    char pool[131072];
    int pooln;
    int err;
    int loop_depth;
    bool break_flag;
    bool return_flag;
    PVal retval;
    int call_depth;
};

static const char* pdup(PVm* V, const char* s);   // fwd

static PVal* vm_get(PVm* V, const char* name) {
    for (int i = 0; i < V->nvars; i++)
        if (strcmp(V->vnames[i], name) == 0) return &V->vars[i];
    return 0;
}
static PVal* vm_set(PVm* V, const char* name, const PVal& v) {
    const char* cp = pdup(V, v.s);
    PVal* p = vm_get(V, name);
    if (p) { p->s = cp; p->len = (int)strlen(cp); return p; }
    if (V->nvars >= 96) return 0;
    int i = V->nvars++;
    strncpy(V->vnames[i], name, 63);
    V->vars[i].s = cp; V->vars[i].len = (int)strlen(cp);
    return &V->vars[i];
}
static const char* pdup(PVm* V, const char* s) {
    if (!s) s = "";
    int n = (int)strlen(s);
    if (V->pooln + n + 1 > (int)sizeof(V->pool)) V->pooln = 0;
    const char* r = V->pool + V->pooln;
    memcpy(V->pool + V->pooln, s, (size_t)n);
    V->pool[V->pooln + n] = 0;
    V->pooln += n + 1;
    return r;
}
static PVal* vm_set_cstr(PVm* V, const char* name, const char* s) {
    return vm_set(V, name, PVal(s));
}

// array encoding helpers: '{' k ':' v ';' ... '}' with \ : ; escaping
static char* pv_arr_encode(PVm* V, const char* k, const char* v) {
    // allocate into V->out? use static scratch per call — instead build via malloc
    int n = 4, cap = 16;
    char* b = (char*)malloc((size_t)cap);
    b[0] = '{'; n = 1;
    for (const char* c = k; *c; c++) {
        if (n + 2 >= cap) { cap *= 2; b = (char*)realloc(b, (size_t)cap); }
        if (*c == '\\' || *c == ':' || *c == ';' || *c == '{' || *c == '}') b[n++] = '\\';
        b[n++] = *c;
    }
    if (n + 2 >= cap) { cap *= 2; b = (char*)realloc(b, (size_t)cap); }
    b[n++] = ':';
    for (const char* c = v; *c; c++) {
        if (n + 2 >= cap) { cap *= 2; b = (char*)realloc(b, (size_t)cap); }
        if (*c == '\\' || *c == ':' || *c == ';' || *c == '{' || *c == '}') b[n++] = '\\';
        b[n++] = *c;
    }
    b[n++] = ';';
    b[n++] = '}';
    b[n] = 0;
    return b;
}

static PVal vm_arr_get(const PVal& arr, const char* key) {
    if (arr.len < 2 || arr.s[0] != '{') return PVal("");
    const char* p = arr.s + 1;
    for (;;) {
        if (*p == '}') break;
        // key
        char k[96]; int kn = 0;
        while (*p && *p != ':' && *p != '}') {
            if (*p == '\\' && p[1]) p++;
            if (kn < 95) k[kn++] = *p;
            p++;
        }
        k[kn] = 0;
        if (*p == ':') p++;
        // value
        char v[512]; int vn = 0;
        while (*p && *p != ';' && *p != '}') {
            if (*p == '\\' && p[1]) p++;
            if (vn < 511) v[vn++] = *p;
            p++;
        }
        v[vn] = 0;
        if (strcmp(k, key) == 0) return PVal(v);
        if (*p == ';') p++;
    }
    return PVal("");
}

struct PScratch { char b[4096]; int n; };
static void pscratch_char(PScratch* S, char c);
static void pscratch_str(PScratch* S, const char* s);

static void vm_arr_set(PVm* V, const char* var, const PVal& idx, const PVal& val) {
    // encode the full array
    PVal cur = vm_get(V, var) ? *vm_get(V, var) : PVal("");
    char key[96];
    if (idx.len == 0) {
        // append: key = largest numeric+1
        int maxk = -1;
        const char* p = cur.s && cur.s[0] == '{' ? cur.s + 1 : 0;
        if (p) {
            for (;;) {
                if (*p == '}' || !*p) break;
                char k[96]; int kn = 0;
                while (*p && *p != ':' && *p != '}') {
                    if (*p == '\\' && p[1]) p++;
                    if (kn < 95) k[kn++] = *p;
                    p++;
                }
                k[kn] = 0;
                if (*p == ':') p++;
                while (*p && *p != ';' && *p != '}') {
                    if (*p == '\\' && p[1]) p++;
                    p++;
                }
                int kv = atoi(k);
                if (k[0] && (kv >= 0 || k[0] == '-')) { if (kv > maxk) maxk = kv; }
                if (*p == ';') p++;
            }
        }
        maxk++;
        snprintf(key, sizeof(key), "%d", maxk);
    } else if (idx.s[0] >= '0' && idx.s[0] <= '9') {
        int iv = atoi(idx.s);
        snprintf(key, sizeof(key), "%d", iv);
    } else {
        strncpy(key, idx.s, 95);
        key[95] = 0;
    }
    // merge: keep existing pairs, add/replace this one before the closing '}'
    char* e = pv_arr_encode(V, key, val.s);   // "{key:val;}"
    PScratch ms; ms.n = 0;
    pscratch_char(&ms, '{');
    const char* oc = cur.s;
    if (oc && oc[0] == '{' && oc[1] && oc[1] != '}') {
        const char* ic = oc + 1;
        int ilen = (int)strlen(ic);
        if (ilen > 0 && ic[ilen - 1] == '}') ilen--;
        while (ilen > 0 && ic[ilen - 1] == ';') ilen--;
        for (int j = 0; j < ilen; j++) pscratch_char(&ms, ic[j]);
        pscratch_char(&ms, ';');
    }
    for (int j = 1; j < (int)strlen(e) - 1; j++) pscratch_char(&ms, e[j]);
    pscratch_char(&ms, '}');
    ms.b[ms.n] = 0;
    vm_set_cstr(V, var, ms.b);
    free(e);
}

static void vm_out_char(PVm* V, char c) {
    if (V->outn + 1 < V->outcap) V->out[V->outn++] = c;
    else V->err = 1;
}
static void vm_out_str(PVm* V, const char* s, int n) {
    if (n < 0) n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        if (V->outn + 1 < V->outcap) V->out[V->outn++] = s[i];
        else { V->err = 1; return; }
    }
}

// string helpers that write into a scratch buffer (fixed 4KB)
static void pscratch_char(PScratch* S, char c) { if (S->n < 4095) S->b[S->n++] = c; }

static PVal vm_stmt(PVm* V, PStmt* st, PScratch* S);  // fwd
static void pscratch_str(PScratch* S, const char* s) { for (; *s; s++) pscratch_char(S, *s); }
static void pscratch_num(PScratch* S, double d) {
    char tmp[64];
    if (d == (double)(long long)d && d < 1e15 && d > -1e15)
        snprintf(tmp, sizeof(tmp), "%lld", (long long)d);
    else
        snprintf(tmp, sizeof(tmp), "%.6f", d);
    pscratch_str(S, tmp);
}

static PVal vm_call_builtin(PVm* V, const char* name, PVal* args, int nargs, PScratch* S);

static PVal vm_eval(PVm* V, PExpr* e, PScratch* S);  // fwd

static PVal vm_eval_inner(PVm* V, PExpr* e, PScratch* S) {
    if (!e) return PVal("");
    switch (e->kind) {
    case PE_LIT:
        return e->lit;
    case PE_VAR: {
        PVal* v = vm_get(V, e->name);
        return v ? *v : PVal("");
    }
    case PE_BIN: {
        PVal a = vm_eval(V, e->a, S);
        PVal b = vm_eval(V, e->b, S);
        switch (e->op) {
        case 0: case 1: case 2: case 3: case 4: {
            double x, y; pv_num(a, &x); pv_num(b, &y);
            double r = 0;
            if (e->op == 0) r = x + y;
            else if (e->op == 1) r = x - y;
            else if (e->op == 2) r = x * y;
            else if (e->op == 3) r = (y != 0) ? x / y : 0;
            else r = (y != 0) ? (double)((long long)x % (long long)y) : 0;
            S->n = 0; pscratch_num(S, r);
            S->b[S->n] = 0;
            return PVal(S->b);
        }
        case 5: {
            S->n = 0;
            pscratch_str(S, a.s ? a.s : "");
            pscratch_str(S, b.s ? b.s : "");
            S->b[S->n] = 0;
            return PVal(S->b);
        }
        default: break;
        }
        // comparisons & logic
        double x = 0, y = 0, da = 0, db = 0;
        pv_num(a, &da); pv_num(b, &db);
        if (e->op == 6) return PVal(da == db ? "1" : "");
        if (e->op == 7) return PVal(da != db ? "1" : "");
        if (e->op >= 6 && e->op <= 11) {
            pv_num(a, &x); pv_num(b, &y);
            bool r = false;
            if (e->op == 6) r = x == y;
            else if (e->op == 7) r = x != y;
            else if (e->op == 8) r = x < y;
            else if (e->op == 9) r = x > y;
            else if (e->op == 10) r = x <= y;
            else r = x >= y;
            return PVal(r ? "1" : "");
        }
        if (e->op == 12) return PVal((pv_truthy(a) && pv_truthy(b)) ? "1" : "");
        if (e->op == 13) return PVal((pv_truthy(a) || pv_truthy(b)) ? "1" : "");
        return PVal("");
    }
    case PE_UN: {
        PVal a = vm_eval(V, e->a, S);
        if (e->op == 0) {
            double x; pv_num(a, &x);
            S->n = 0; pscratch_num(S, -x);
            S->b[S->n] = 0;
            return PVal(S->b);
        }
        if (e->op == 1) return PVal(pv_truthy(a) ? "" : "1");
        if (e->op == 2) {
            // prefix ++$x
            if (e->a && e->a->kind == PE_VAR) {
                PVal* v = vm_get(V, e->a->name);
                double x = 0; if (v) pv_num(*v, &x); x++;
                S->n = 0; pscratch_num(S, x);
                S->b[S->n] = 0;
                if (v) { const char* np = pdup(V, S->b); v->s = np; v->len = (int)strlen(np); }
                return PVal(S->b);
            }
        }
        return PVal("");
    }
    case PE_TERN: {
        PVal c = vm_eval(V, e->a, S);
        return pv_truthy(c) ? vm_eval(V, e->b, S) : vm_eval(V, e->c ? e->c : e->a, S);
    }
    case PE_IDX: {
        PVal base = vm_get(V, e->name) ? *vm_get(V, e->name) : PVal("");
        PScratch S2; S2.n = 0;
        PVal idx = vm_eval(V, e->a, &S2);
        S2.b[S2.n] = 0;
        return vm_arr_get(base, idx.s);
    }
    case PE_ARR: {
        // array literal -> encoded array string.
        // Parser flags a pair's key with name[0]=='K'; those consume two args.
        // Accumulate into the caller's scratch so the returned pointer outlives
        // this frame (a local scratch would dangle by the time vm_eval's
        // stable-pool copy runs).
        S->n = 0;
        pscratch_char(S, '{');
        int key = 0;
        for (int i = 0; i < e->nargs; ) {
            const char* kk;
            PVal vv;
            if (e->args[i]->name[0] == 'K' && e->args[i]->name[1] == 0 && i + 1 < e->nargs) {
                PScratch tk; tk.n = 0;
                PVal k = vm_eval(V, e->args[i], &tk);
                tk.b[tk.n] = 0;
                PScratch tv; tv.n = 0;
                vv = vm_eval(V, e->args[i + 1], &tv);
                tv.b[tv.n] = 0;
                kk = k.s ? k.s : "";   // evaluated pointer (pool), not tk.b
                i += 2;
            } else {
                PScratch tv; tv.n = 0;
                vv = vm_eval(V, e->args[i], &tv);
                tv.b[tv.n] = 0;
                char kb[16]; snprintf(kb, sizeof(kb), "%d", key++);
                kk = kb;
                i += 1;
            }
            char* pair = pv_arr_encode(V, kk, vv.s);
            // pair is "{k:v;}" — its inner payload already ends with ';',
            // so just append it; the closing '}' is written once after the
            // loop (writing it per-iteration left '}' mid-string).
            for (int j = 1; j < (int)strlen(pair) - 1; j++) pscratch_char(S, pair[j]);
            free(pair);
        }
        pscratch_char(S, '}');   // close once after all pairs ("{}" for empty)
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    case PE_CALL: {
        // user function?
        for (PFunc* f = *V->fns; f; f = f->next) {
            if (strcmp(f->name, e->name) == 0) {
                if (V->call_depth > 8) { V->err = 1; return PVal(""); }
                // evaluate args
                PVal av[8];
                PScratch tmp;
                for (int i = 0; i < e->nargs && i < 8; i++) {
                    tmp.n = 0;
                    av[i] = vm_eval(V, e->args[i], &tmp);
                    tmp.b[tmp.n] = 0;
                    // vm_eval returned a stable pool pointer; make a heap copy
                    // so the callee's scratch churn cannot corrupt it
                    int alen = av[i].len > 0 ? av[i].len : (int)strlen(av[i].s);
                    char* cp = (char*)malloc((size_t)alen + 1);
                    memcpy(cp, av[i].s, (size_t)alen);
                    cp[alen] = 0;
                    av[i].s = cp; av[i].len = alen;
                }
                // save scope
                PVal svars[96]; char svnames[96][64]; int snvars = V->nvars;
                memcpy(svars, V->vars, sizeof(PVal) * (size_t)V->nvars);
                memcpy(svnames, V->vnames, sizeof(svnames));
                V->nvars = 0;
                for (int i = 0; i < f->nparams && i < e->nargs; i++) {
                    vm_set_cstr(V, f->params[i], av[i].s);
                }
                // execute body with return capture
                bool rr = V->return_flag;
                V->return_flag = false;
                V->call_depth++;
                for (int i = 0; i < f->nbody && !V->return_flag && !V->err; i++) {
                    PVal r = vm_stmt(V, f->body[i], S);
                    if (V->return_flag) { S->n = 0; pscratch_str(S, r.s ? r.s : ""); S->b[S->n] = 0; }
                }
                V->call_depth--;
                PVal ret = V->retval;
                V->return_flag = rr;
                // restore scope
                V->nvars = snvars;
                memcpy(V->vars, svars, sizeof(PVal) * (size_t)snvars);
                memcpy(V->vnames, svnames, sizeof(svnames));
                for (int i = 0; i < e->nargs && i < 8; i++) free((void*)av[i].s);
                return ret;
            }
        }
        // builtin
        PVal av[8];
        for (int i = 0; i < e->nargs && i < 8; i++) {
            PScratch t2; t2.n = 0;
            av[i] = vm_eval(V, e->args[i], &t2);
            t2.b[t2.n] = 0;
            // keep the evaluated pointer (vm_eval copied it into the stable
            // pool); overwriting it with t2.b would hand the callee garbage
        }
        PScratch bo; bo.n = 0;
        PVal r = vm_call_builtin(V, e->name, av, e->nargs > 8 ? 8 : e->nargs, &bo);
        bo.b[bo.n] = 0;
        return r;
    }
    }
    return PVal("");
}

// public wrapper: every value that came out of a scratch buffer is copied into
// the stable pool so callers can safely hold it across further evaluation.
static PVal vm_eval(PVm* V, PExpr* e, PScratch* S) {
    PVal r = vm_eval_inner(V, e, S);
    return PVal(pdup(V, r.s));
}

// builtins
static PVal vm_call_builtin(PVm* V, const char* name, PVal* args, int nargs, PScratch* S) {
    S->n = 0;
    // date: format chars
    if (strcmp(name, "date") == 0) {
        if (nargs < 1) return PVal("");
        nefu::DateInfo di;
        if (!nefu::platform_rtc_date(&di)) { di.year = 2026; di.month = 9; di.day = 25; di.hour = 0; di.min = 0; di.sec = 0; }
        for (const char* c = args[0].s; *c; c++) {
            char f = *c;
            if (f == '\\' && c[1]) { pscratch_char(S, c[1]); c++; continue; }
            char t[8];
            if (f == 'Y') snprintf(t, sizeof(t), "%04d", di.year);
            else if (f == 'y') snprintf(t, sizeof(t), "%02d", di.year % 100);
            else if (f == 'm') snprintf(t, sizeof(t), "%02d", di.month);
            else if (f == 'n') snprintf(t, sizeof(t), "%d", di.month);
            else if (f == 'd') snprintf(t, sizeof(t), "%02d", di.day);
            else if (f == 'j') snprintf(t, sizeof(t), "%d", di.day);
            else if (f == 'H') snprintf(t, sizeof(t), "%02d", di.hour);
            else if (f == 'i') snprintf(t, sizeof(t), "%02d", di.min);
            else if (f == 's') snprintf(t, sizeof(t), "%02d", di.sec);
            else { t[0] = f; t[1] = 0; }
            pscratch_str(S, t);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "strlen") == 0) {
        int n = nargs > 0 ? (int)strlen(args[0].s ? args[0].s : "") : 0;
        char t[16]; snprintf(t, sizeof(t), "%d", n);
        pscratch_str(S, t);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "strtoupper") == 0) {
        for (const char* c = nargs > 0 ? args[0].s : ""; *c; c++) {
            char ch = *c;
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            pscratch_char(S, ch);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "strtolower") == 0) {
        for (const char* c = nargs > 0 ? args[0].s : ""; *c; c++) {
            char ch = *c;
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            pscratch_char(S, ch);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "ucfirst") == 0 || strcmp(name, "ucwords") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        bool up = true;
        for (const char* c = src; *c; c++) {
            char ch = *c;
            if (strcmp(name, "ucwords") == 0) {
                if (up && ch >= 'a' && ch <= 'z') ch -= 32;
                up = (ch == ' ' || ch == '-' || ch == '_');
            } else {
                if (c == src && ch >= 'a' && ch <= 'z') ch -= 32;
            }
            pscratch_char(S, ch);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "substr") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        int len = (int)strlen(src);
        int start = nargs > 1 ? atoi(args[1].s) : 0;
        int count = nargs > 2 ? atoi(args[2].s) : len;
        if (start < 0) start = len + start;
        if (count < 0) count = len - start + count;
        if (start < 0) start = 0;
        if (start > len) start = len;
        for (int i = start; i < len && count > 0; i++, count--) pscratch_char(S, src[i]);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "trim") == 0 || strcmp(name, "ltrim") == 0 || strcmp(name, "rtrim") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        const char* a = src;
        const char* b = src + strlen(src);
        if (strcmp(name, "ltrim") != 0)
            while (*a && (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r')) a++;
        if (strcmp(name, "rtrim") != 0)
            while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\n' || b[-1] == '\r')) b--;
        for (const char* c = a; c < b; c++) pscratch_char(S, *c);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "str_replace") == 0) {
        const char* from = nargs > 0 ? args[0].s : "";
        const char* to = nargs > 1 ? args[1].s : "";
        const char* src = nargs > 2 ? args[2].s : "";
        int fl = (int)strlen(from);
        for (const char* c = src; *c; ) {
            if (fl && strncmp(c, from, (size_t)fl) == 0) {
                pscratch_str(S, to);
                c += fl;
            } else {
                pscratch_char(S, *c);
                c++;
            }
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "strpos") == 0) {
        const char* h = nargs > 0 ? args[0].s : "";
        const char* n = nargs > 1 ? args[1].s : "";
        const char* f = strstr(h, n);
        int r = f ? (int)(f - h) : -1;
        char t[16]; snprintf(t, sizeof(t), "%d", r);
        pscratch_str(S, t);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "str_repeat") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        int n = nargs > 1 ? atoi(args[1].s) : 0;
        for (int i = 0; i < n && S->n < 4000; i++) pscratch_str(S, src);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "count") == 0 || strcmp(name, "sizeof") == 0) {
        const char* arr = nargs > 0 ? args[0].s : "";
        int c = 0;
        if (arr && arr[0] == '{') {
            const char* p = arr + 1;
            while (*p && *p != '}') {
                while (*p && *p != ':') { if (*p == '\\' && p[1]) p++; p++; }
                if (*p == ':') p++;
                while (*p && *p != ';' && *p != '}') { if (*p == '\\' && p[1]) p++; p++; }
                c++;
                if (*p == ';') p++;
            }
        }
        char t[16]; snprintf(t, sizeof(t), "%d", c);
        pscratch_str(S, t);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "implode") == 0 || strcmp(name, "join") == 0) {
        const char* glue = nargs > 1 ? args[0].s : "";
        const char* arr = nargs > 1 ? args[1].s : args[0].s;
        bool first = true;
        if (arr && arr[0] == '{') {
            const char* p = arr + 1;
            while (*p && *p != '}') {
                while (*p && *p != ':') { if (*p == '\\' && p[1]) p++; p++; }
                if (*p == ':') p++;
                if (!first) pscratch_str(S, glue);
                while (*p && *p != ';' && *p != '}') {
                    if (*p == '\\' && p[1]) p++;
                    pscratch_char(S, *p);
                    p++;
                }
                first = false;
                if (*p == ';') p++;
            }
        } else pscratch_str(S, arr);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "explode") == 0) {
        const char* sep = nargs > 0 ? args[0].s : "";
        const char* src = nargs > 1 ? args[1].s : "";
        PScratch A; A.n = 0;
        pscratch_char(&A, '{');
        int key = 0;
        // tokenize
        const char* p = src;
        const char* q = src;
        bool done = false;
        while (!done) {
            const char* f = strstr(p, sep);
            const char* end = f ? f : p + strlen(p);
            done = !f;
            char kb[16]; snprintf(kb, sizeof(kb), "%d", key++);
            // encode one pair into A
            char* pair = pv_arr_encode(V, kb, "");
            // rebuild pair with value = [p,end)
            // (encode manually)
            int plen = (int)strlen(pair);
            // crude: replace trailing ";" with value + ";"}
            int cap = plen + (int)(end - p) + 4;
            char* b2 = (char*)malloc((size_t)cap);
            int n = 0;
            b2[n++] = '{';
            for (const char* c = kb; *c; c++) { if (n + 1 < cap) b2[n++] = *c; }
            b2[n++] = ':';
            for (const char* c = p; c < end; c++) {
                if (*c == '\\' || *c == ':' || *c == ';') { if (n + 1 < cap) b2[n++] = '\\'; }
                if (n + 1 < cap) b2[n++] = *c;
            }
            b2[n++] = ';';
            b2[n++] = '}';
            b2[n] = 0;
            free(pair);
            // append to A (drop A's '{'/'}')
            if (A.n == 1) { A.n = 0; }
            else if (A.n > 1) { A.b[A.n - 1] = ';'; }  // replace '}' with ';'
            for (int i = 1; i < n - 1; i++) pscratch_char(&A, b2[i]);
            pscratch_char(&A, '}');
            free(b2);
            if (f) p = f + strlen(sep);
        }
        A.b[A.n] = 0;
        return PVal(A.b);
    }
    if (strcmp(name, "isset") == 0) {
        if (nargs < 1 || args[0].s == 0) return PVal("");
        // isset works on variables — args evaluated to "" if unset
        return PVal(args[0].s[0] ? "1" : "");
    }
    if (strcmp(name, "empty") == 0) {
        bool t = nargs > 0 && args[0].s && args[0].s[0];
        if (t && args[0].s[0] == '0' && args[0].s[1] == 0) t = false;
        return PVal(t ? "" : "1");
    }
    if (strcmp(name, "intval") == 0 || strcmp(name, "int") == 0) {
        int r = nargs > 0 ? atoi(args[0].s ? args[0].s : "") : 0;
        char t[16]; snprintf(t, sizeof(t), "%d", r);
        pscratch_str(S, t);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "floatval") == 0) {
        double d = nargs > 0 ? atof(args[0].s ? args[0].s : "") : 0;
        pscratch_num(S, d);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "abs") == 0) {
        double d = nargs > 0 ? atof(args[0].s ? args[0].s : "") : 0;
        pscratch_num(S, d < 0 ? -d : d);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
        double best = nargs > 0 ? atof(args[0].s ? args[0].s : "") : 0;
        for (int i = 1; i < nargs; i++) {
            double d = atof(args[i].s ? args[i].s : "");
            if (strcmp(name, "min") == 0) { if (d < best) best = d; }
            else { if (d > best) best = d; }
        }
        pscratch_num(S, best);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "round") == 0 || strcmp(name, "ceil") == 0 || strcmp(name, "floor") == 0) {
        double d = nargs > 0 ? atof(args[0].s ? args[0].s : "") : 0;
        if (strcmp(name, "ceil") == 0) d = (double)(int)(d + 0.99999);
        else if (strcmp(name, "floor") == 0) d = (double)(int)d;
        else d = (double)(int)(d + 0.5);
        pscratch_num(S, d);
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "number_format") == 0) {
        double d = nargs > 0 ? atof(args[0].s ? args[0].s : "") : 0;
        char t[64];
        snprintf(t, sizeof(t), "%lld", (long long)d);
        // insert commas
        int tl = (int)strlen(t);
        int neg = (t[0] == '-');
        int digits = tl - neg;
        int comma = digits > 3 ? (digits - 1) / 3 : 0;
        int wn = 0;
        if (neg) pscratch_char(S, '-');
        for (int i = 0; i < digits; i++) {
            if (comma && i > 0 && (digits - i) % 3 == 0) pscratch_char(S, ',');
            pscratch_char(S, t[neg + i]);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "htmlspecialchars") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        for (const char* c = src; *c; c++) {
            if (*c == '&') pscratch_str(S, "&amp;");
            else if (*c == '<') pscratch_str(S, "&lt;");
            else if (*c == '>') pscratch_str(S, "&gt;");
            else if (*c == '"') pscratch_str(S, "&quot;");
            else pscratch_char(S, *c);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    if (strcmp(name, "str_pad") == 0) {
        const char* src = nargs > 0 ? args[0].s : "";
        int w = nargs > 1 ? atoi(args[1].s) : 0;
        const char* pad = nargs > 2 ? args[2].s : " ";
        int sl = (int)strlen(src);
        if (sl >= w) pscratch_str(S, src);
        else {
            pscratch_str(S, src);
            for (int i = sl; i < w; i++) pscratch_char(S, pad[i % strlen(pad)]);
        }
        S->b[S->n] = 0;
        return PVal(S->b);
    }
    // unknown builtin -> empty
    return PVal("");
}

static PVal vm_stmt(PVm* V, PStmt* st, PScratch* S) {
    if (!st) return PVal("");
    switch (st->kind) {
    case PS_NOP:
        for (int i = 0; i < st->nbody; i++) vm_stmt(V, st->body[i], S);
        return PVal("");
    case PS_ASSIGN: {
        PVal v = vm_eval(V, st->e, S);
        vm_set_cstr(V, st->name, v.s ? v.s : "");
        return PVal("");
    }
    case PS_ASSIGNOP: {
        PVal cur = vm_get(V, st->name) ? *vm_get(V, st->name) : PVal("");
        PVal r = vm_eval(V, st->e, S);
        double x = 0, y = 0;
        if (st->op == 5) {
            PScratch t2; t2.n = 0;
            pscratch_str(&t2, cur.s ? cur.s : "");
            pscratch_str(&t2, r.s ? r.s : "");
            t2.b[t2.n] = 0;
            vm_set_cstr(V, st->name, t2.b);
        } else {
            pv_num(cur, &x); pv_num(r, &y);
            double n = x;
            if (st->op == 0) n = x + y;
            else if (st->op == 1) n = x - y;
            else if (st->op == 2) n = x * y;
            else if (st->op == 3) n = y ? x / y : 0;
            PScratch t2; t2.n = 0;
            pscratch_num(&t2, n);
            t2.b[t2.n] = 0;
            vm_set_cstr(V, st->name, t2.b);
        }
        return PVal("");
    }
    case PS_INCDEC: {
        PVal cur = vm_get(V, st->name) ? *vm_get(V, st->name) : PVal("0");
        double x = 0; pv_num(cur, &x);
        x += (st->op == 2) ? 1.0 : -1.0;
        PScratch t2; t2.n = 0;
        pscratch_num(&t2, x);
        t2.b[t2.n] = 0;
        vm_set_cstr(V, st->name, t2.b);
        return PVal("");
    }
    case PS_ECHO: {
        // echo a[, b ...]: stored as PE_ARR of args
        if (st->e && st->e->kind == PE_ARR) {
            for (int i = 0; i < st->e->nargs; i++) {
                PScratch t2; t2.n = 0;
                PVal v = vm_eval(V, st->e->args[i], &t2);
                t2.b[t2.n] = 0;
                vm_out_str(V, v.s ? v.s : "", v.s ? (int)strlen(v.s) : 0);
            }
        } else {
            PScratch t2; t2.n = 0;
            PVal v = vm_eval(V, st->e, &t2);
            t2.b[t2.n] = 0;
            vm_out_str(V, v.s ? v.s : "", v.s ? (int)strlen(v.s) : 0);
        }
        return PVal("");
    }
    case PS_IF: {
        PScratch t2; t2.n = 0;
        PVal c = vm_eval(V, st->e, &t2);
        t2.b[t2.n] = 0;
        if (pv_truthy(c)) {
            for (int i = 0; i < st->nbody; i++) vm_stmt(V, st->body[i], S);
        } else if (st->els) {
            if (st->els->kind == PS_IF) vm_stmt(V, st->els, S);
            else for (int i = 0; i < st->els->nbody; i++) vm_stmt(V, st->els->body[i], S);
        }
        return PVal("");
    }
    case PS_WHILE: {
        int guard = 0;
        while (guard++ < 100000) {
            PScratch t2; t2.n = 0;
            PVal c = vm_eval(V, st->e, &t2);
            t2.b[t2.n] = 0;
            if (!pv_truthy(c)) break;
            for (int i = 0; i < st->nbody; i++) {
                vm_stmt(V, st->body[i], S);
                if (V->break_flag) { V->break_flag = false; return PVal(""); }
                if (V->return_flag) return V->retval;
                if (V->err) return PVal("");
            }
        }
        return PVal("");
    }
    case PS_FOR: {
        for (int i = 0; i < st->ninit; i++) vm_stmt(V, st->init[i], S);
        int guard = 0;
        for (;;) {
            if (guard++ > 100000) { V->err = 1; break; }
            PScratch t2; t2.n = 0;
            PVal c = st->e ? vm_eval(V, st->e, &t2) : PVal("1");
            t2.b[t2.n] = 0;
            if (st->e && !pv_truthy(c)) break;
            for (int i = 0; i < st->nbody; i++) {
                vm_stmt(V, st->body[i], S);
                if (V->break_flag) { V->break_flag = false; return PVal(""); }
                if (V->return_flag) return V->retval;
                if (V->err) return PVal("");
            }
            if (st->step) {
                PScratch t3; t3.n = 0;
                vm_eval(V, st->step, &t3);
                t3.b[t3.n] = 0;
            }
        }
        return PVal("");
    }
    case PS_FOREACH: {
        PScratch t2; t2.n = 0;
        PVal arr = vm_eval(V, st->e, &t2);
        t2.b[t2.n] = 0;
        if (arr.s && arr.s[0] == '{') {
            const char* p = arr.s + 1;
            int guard = 0;
            while (*p && *p != '}' && guard++ < 100000) {
                char k[96]; int kn = 0;
                while (*p && *p != ':' && *p != '}') {
                    if (*p == '\\' && p[1]) p++;
                    if (kn < 95) k[kn++] = *p;
                    p++;
                }
                k[kn] = 0;
                if (*p == ':') p++;
                char v[512]; int vn = 0;
                while (*p && *p != ';' && *p != '}') {
                    if (*p == '\\' && p[1]) p++;
                    if (vn < 511) v[vn++] = *p;
                    p++;
                }
                v[vn] = 0;
                if (st->kv[0]) vm_set_cstr(V, st->kv, k);
                vm_set_cstr(V, st->name, v);
                for (int i = 0; i < st->nbody; i++) {
                    vm_stmt(V, st->body[i], S);
                    if (V->break_flag) { V->break_flag = false; return PVal(""); }
                    if (V->return_flag) return V->retval;
                    if (V->err) return PVal("");
                }
                if (*p == ';') p++;
            }
        }
        return PVal("");
    }
    case PS_RETURN: {
        PScratch t2; t2.n = 0;
        PVal r = st->e ? vm_eval(V, st->e, &t2) : PVal("");
        t2.b[t2.n] = 0;
        // vm_eval returned a stable pool pointer; do NOT read t2.b (scratch
        // holds garbage for PE_VAR/PE_LIT) — copy the evaluated value itself.
        V->retval = PVal(pdup(V, r.s ? r.s : ""));
        V->return_flag = true;
        return V->retval;
    }
    case PS_EXPR: {
        PScratch t2; t2.n = 0;
        vm_eval(V, st->e, &t2);
        t2.b[t2.n] = 0;
        return PVal("");
    }
    case PS_BREAK:
        V->break_flag = true;
        return PVal("");
    case PS_ARRSET: {
        PScratch t2; t2.n = 0;
        PVal idx = st->e ? vm_eval(V, st->e, &t2) : PVal("");
        t2.b[t2.n] = 0;
        PScratch t3; t3.n = 0;
        PVal val = st->c ? vm_eval(V, st->c, &t3) : PVal("");
        t3.b[t3.n] = 0;
        if (st->op == 5) {
            PVal cur = vm_arr_get(vm_get(V, st->name) ? *vm_get(V, st->name) : PVal(""), idx.s);
            PScratch t4; t4.n = 0;
            pscratch_str(&t4, cur.s);
            pscratch_str(&t4, t3.b);
            t4.b[t4.n] = 0;
            vm_arr_set(V, st->name, idx, PVal(t4.b));
        } else {
            vm_arr_set(V, st->name, idx, val);
        }
        return PVal("");
    }
    }
    return PVal("");
}

// ---------------------------------------------------------------------------
// top-level: scan <?php / <?= / <? blocks and execute
// ---------------------------------------------------------------------------
// helpers for the pending-block buffer
static void pend_write(char* pend, int* n, int cap, const char* s, int len) {
    if (len < 0) len = (int)strlen(s);
    if (*n + len >= cap) len = cap - *n - 1;
    if (len > 0) { memcpy(pend + *n, s, (size_t)len); *n += len; }
    pend[*n] = 0;
}
// literal HTML inside an open template block becomes a per-iteration echo
static void pend_echo(char* pend, int* n, int cap, const char* s, int len) {
    const char* hdr = "echo(\"";
    pend_write(pend, n, cap, hdr, -1);
    for (int i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '"' || ch == '\\') {
            if (*n + 1 < cap) { pend[(*n)++] = '\\'; }
        }
        if (*n < cap - 1) pend[(*n)++] = ch;
    }
    const char* ft = "\");";
    pend_write(pend, n, cap, ft, -1);
}

static int php_convert(const char* src, char* out, int out_sz) {
    PVm V;
    memset(&V, 0, sizeof(V));
    V.out = out; V.outcap = out_sz;
    PFunc* fns = 0;
    V.fns = &fns;

    char pend[16384]; int pendn = 0;   // pending open-block source

    const char* p = src;
    while (*p && !V.err) {
        // find next php open tag
        const char* q = strstr(p, "<?");
        if (!q) {
            // tail text
            if (pendn > 0) {
                pend_echo(pend, &pendn, (int)sizeof(pend), p, (int)strlen(p));
            } else {
                vm_out_str(&V, p, (int)strlen(p));
            }
            break;
        }
        // text before the tag
        if (q > p) {
            if (pendn > 0) pend_echo(pend, &pendn, (int)sizeof(pend), p, (int)(q - p));
            else vm_out_str(&V, p, (int)(q - p));
        }
        q += 2;
        bool short_echo = false;
        if (q[0] == 'p' && q[1] == 'h' && q[2] == 'p') {
            q += 3;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        } else if (q[0] == '=') {
            q++;
            short_echo = true;
        } else if (q[0] == 'x') {
            // <?xml ... ?> — not PHP; copy as text
            vm_out_char(&V, '<');
            vm_out_char(&V, '?');
            p = q;
            continue;
        } else {
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        }
        const char* close = strstr(q, "?>");
        if (!close) close = p + strlen(p);   // to EOF
        int clen = (int)(close - q);
        char* code = (char*)malloc((size_t)clen + 1);
        memcpy(code, q, (size_t)clen);
        code[clen] = 0;

        if (short_echo) {
            if (pendn > 0) {
                // inside an open block: record as an echo statement
                char ex[512];
                snprintf(ex, sizeof(ex), "echo(%s);", code);
                pend_write(pend, &pendn, (int)sizeof(pend), ex, -1);
            } else {
                PLex Lx; lex_init(&Lx, code);
                PParser P; P.L = &Lx; P.fns = &fns; P.fail = false;
                PExpr* e = pp_expr(&P);
                if (e && !P.fail) {
                    PScratch S; S.n = 0;
                    PVal v = vm_eval(&V, e, &S);
                    // PE_VAR / literal evals never write the scratch, so the
                    // result value (stable pool pointer) is the only truth.
                    vm_out_str(&V, v.s ? v.s : "", v.s ? (int)strlen(v.s) : 0);
                }
                pe_free(e);
            }
        } else {
            int tlen = pendn;
            int mlen = tlen + clen;
            char* merged = (char*)malloc((size_t)mlen + 1);
            if (tlen > 0) memcpy(merged, pend, (size_t)tlen);
            memcpy(merged + tlen, code, (size_t)clen);
            merged[mlen] = 0;
            pendn = 0;
            // brace balance (rough; template text rarely holds braces in strings)
            int ob = 0, cb = 0;
            for (int i = 0; i < mlen; i++) {
                if (merged[i] == '{') ob++;
                else if (merged[i] == '}') cb++;
            }
            if (ob > cb) {
                // block still open — hold everything for the next block
                if (mlen < (int)sizeof(pend)) {
                    memcpy(pend, merged, (size_t)mlen);
                    pend[mlen] = 0;
                    pendn = mlen;
                }
                free(merged);
            } else {
                PLex Lx; lex_init(&Lx, merged);
                PParser P; P.L = &Lx; P.fns = &fns; P.fail = false;
                PProg prog;
                if (pp_parse(&P, &prog)) {
                    PScratch S; S.n = 0;
                    for (int i = 0; i < prog.nstmts && !V.err; i++) {
                        vm_stmt(&V, prog.stmts[i], &S);
                        S.n = 0;
                    }
                } else {
                    /* parse failed (logged via klogf below) */
                }
                if (P.fail) {
                    // log only — never to the page
                    nefu::klogf("browser: php: syntax error near line %d in %.60s", Lx.line, merged);
                    V.err = 0;   // non-fatal: continue with the rest of the document
                }
                ps_array_free(prog.stmts, prog.nstmts);
                free(merged);
            }
        }
        free(code);
        p = close;
        if (*close) p += 2;   // skip ?>
    }
    if (pendn > 0) {
        // dangling open block at EOF: run what we have
        PLex Lx; lex_init(&Lx, pend);
        PParser P; P.L = &Lx; P.fns = &fns; P.fail = false;
        PProg prog;
        if (pp_parse(&P, &prog)) {
            PScratch S; S.n = 0;
            for (int i = 0; i < prog.nstmts && !V.err; i++) {
                vm_stmt(&V, prog.stmts[i], &S);
                S.n = 0;
            }
        }
        ps_array_free(prog.stmts, prog.nstmts);
    }
    // free function table
    while (fns) {
        PFunc* nxt = fns->next;
        ps_array_free(fns->body, fns->nbody);
        free(fns);
        fns = nxt;
    }
    V.out[V.outn] = 0;
    if (V.outn >= out_sz - 1) V.outn = out_sz - 1;
    return V.outn;
}

} // namespace nfuphp

#endif // NEFU_PHP2HTML_H
