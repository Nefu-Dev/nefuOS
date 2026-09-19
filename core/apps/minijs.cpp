// nefuOS mini JS engine - minimal embeddable JavaScript-like interpreter.
// Numbers are integers (kernel has no FPU path), strings are fixed buffers.
// Parser structure inspired by "Let's Build a Simple Interpreter" (R. Spivak,
// MIT) and tiny-js (MIT); implementation is original, kernel-friendly (no
// dynamic allocation). Powers the browser <script> blocks and terminal `js`.
#include "minijs.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

const int MAX_VARS = 64;
const int OUT_CAP = 512;

struct MVal {
    int t;        // 0 = number, 1 = string
    int num;
    char str[80];
};

struct MJs {
    const char* p;
    char* out;
    int out_sz;
    int oi;
    int err;

    struct Var { char name[20]; MVal v; } vars[MAX_VARS];
    int nvars;

    // ---- token helpers ----
    void skip_ws() {
        while (*p) {
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; continue; }
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
            break;
        }
    }
    bool peek_op(const char* op) {
        const char* save = p;
        skip_ws();
        for (const char* q = op; *q; q++) if (p[q - op] != *q) { p = save; return false; }
        return true;
    }
    void eat_op(const char* op) { skip_ws(); p += (int)strlen(op); }
    bool is_digit(char c) { return c >= '0' && c <= '9'; }
    bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    bool is_ident(char c) { return is_ident_start(c) || is_digit(c); }

    // ---- output ----
    void emit(char c) { if (oi < out_sz - 1) out[oi++] = c; }
    void emit_str(const char* s) { while (*s && oi < out_sz - 1) out[oi++] = *s++; }
    void emit_num(int v) {
        char b[16];
        ksprintf(b, sizeof(b), "%d", v);
        emit_str(b);
    }

    // ---- values ----
    void set_num(MVal& v, int n) { v.t = 0; v.num = n; v.str[0] = 0; }
    void set_str(MVal& v, const char* s) {
        v.t = 1; v.num = 0;
        int n = 0;
        while (s[n] && n < 78) { v.str[n] = s[n]; n++; }
        v.str[n] = 0;
    }
    MVal* find_var(const char* name) {
        for (int i = 0; i < nvars; i++) if (strcmp(vars[i].name, name) == 0) return &vars[i].v;
        return 0;
    }
    MVal* declare_var(const char* name) {
        MVal* v = find_var(name);
        if (v) return v;
        if (nvars >= MAX_VARS) return 0;
        int n = 0;
        while (name[n] && n < 18) { vars[nvars].name[n] = name[n]; n++; }
        vars[nvars].name[n] = 0;
        set_num(vars[nvars].v, 0);
        return &vars[nvars++].v;
    }

    // ---- expression grammar (recursive descent) ----
    MVal primary() {
        MVal v; set_num(v, 0);
        skip_ws();
        if (err) return v;
        if (is_digit(*p)) {
            int n = 0;
            while (is_digit(*p)) { n = n * 10 + (*p - '0'); p++; }
            set_num(v, n);
            return v;
        }
        if (*p == '"' || *p == '\'') {
            char q = *p; p++;
            char b[80]; int n = 0;
            while (*p && *p != q && n < 78) { b[n++] = *p++; }
            if (*p == q) p++;
            b[n] = 0;
            set_str(v, b);
            return v;
        }
        if (*p == '(') {
            p++;
            v = eval_or();
            skip_ws();
            if (*p == ')') p++;
            return v;
        }
        if (is_ident_start(*p)) {
            char name[20]; int n = 0;
            while (is_ident(*p) && n < 18) name[n++] = *p++;
            name[n] = 0;
            skip_ws();
            // built-in function call: print(...) / alert(...)
            if (strcmp(name, "print") == 0 || strcmp(name, "alert") == 0) {
                if (*p == '(') {
                    p++;
                    MVal arg; set_num(arg, 0);
                    skip_ws();
                    if (*p != ')') arg = eval_or();
                    skip_ws();
                    if (*p == ')') p++;
                    if (arg.t == 1) emit_str(arg.str);
                    else emit_num(arg.num);
                    emit('\n');
                    return v;
                }
                err = 1;
                return v;
            }
            // document.write(...)
            if (strcmp(name, "document") == 0 && peek_op(".")) {
                eat_op(".");
                char m[20]; int mn = 0;
                while (is_ident(*p) && mn < 18) m[mn++] = *p++;
                m[mn] = 0;
                skip_ws();
                if (strcmp(m, "write") == 0 && *p == '(') {
                    p++;
                    MVal arg; set_num(arg, 0);
                    skip_ws();
                    if (*p != ')') arg = eval_or();
                    skip_ws();
                    if (*p == ')') p++;
                    if (arg.t == 1) emit_str(arg.str);
                    else emit_num(arg.num);
                    return v;
                }
                err = 1;
                return v;
            }
            // len(...)
            if (strcmp(name, "len") == 0 && *p == '(') {
                p++;
                MVal arg; set_num(arg, 0);
                skip_ws();
                if (*p != ')') arg = eval_or();
                skip_ws();
                if (*p == ')') p++;
                if (arg.t == 1) set_num(v, (int)strlen(arg.str));
                else set_num(v, 0);
                return v;
            }
            MVal* f = find_var(name);
            if (f) return *f;
            err = 1;   // undefined variable
            return v;
        }
        err = 1;
        return v;
    }
    MVal eval_unary() {
        MVal v; set_num(v, 0);
        skip_ws();
        if (peek_op("!")) { eat_op("!"); v = eval_unary(); v.t = 0; v.num = !v.num; return v; }
        if (peek_op("-")) { eat_op("-"); v = eval_unary(); v.num = -v.num; return v; }
        return primary();
    }
    MVal eval_mul() {
        MVal v = eval_unary();
        for (;;) {
            if (peek_op("*")) { eat_op("*"); MVal r = eval_unary(); v.num = v.num * r.num; v.t = 0; }
            else if (peek_op("/")) {
                eat_op("/"); MVal r = eval_unary();
                if (r.num != 0) v.num = v.num / r.num;
                else { v.num = 0; err = 1; }
                v.t = 0;
            } else if (peek_op("%")) { eat_op("%"); MVal r = eval_unary(); v.num = r.num ? v.num % r.num : 0; v.t = 0; }
            else break;
        }
        return v;
    }
    MVal eval_add() {
        MVal v = eval_mul();
        for (;;) {
            if (peek_op("+")) {
                eat_op("+");
                MVal r = eval_mul();
                if (v.t == 1 || r.t == 1) {
                    char b[96]; int n = 0;
                    if (v.t == 1) { for (int i = 0; v.str[i] && n < 88; i++) b[n++] = v.str[i]; }
                    else { char nb[16]; ksprintf(nb, sizeof(nb), "%d", v.num); for (int i = 0; nb[i] && n < 88; i++) b[n++] = nb[i]; }
                    if (r.t == 1) { for (int i = 0; r.str[i] && n < 88; i++) b[n++] = r.str[i]; }
                    else { char nb[16]; ksprintf(nb, sizeof(nb), "%d", r.num); for (int i = 0; nb[i] && n < 88; i++) b[n++] = nb[i]; }
                    b[n] = 0;
                    set_str(v, b);
                } else {
                    v.num = v.num + r.num;
                }
            } else if (peek_op("-")) {
                eat_op("-");
                MVal r = eval_mul();
                if (v.t == 0 && r.t == 0) v.num = v.num - r.num;
                else err = 1;
            } else break;
        }
        return v;
    }
    MVal eval_rel() {
        MVal v = eval_add();
        for (;;) {
            if (peek_op("<=")) { eat_op("<="); MVal r = eval_add(); v.t = 0; v.num = v.num <= r.num; }
            else if (peek_op(">=")) { eat_op(">="); MVal r = eval_add(); v.t = 0; v.num = v.num >= r.num; }
            else if (peek_op("<")) { eat_op("<"); MVal r = eval_add(); v.t = 0; v.num = v.num < r.num; }
            else if (peek_op(">")) { eat_op(">"); MVal r = eval_add(); v.t = 0; v.num = v.num > r.num; }
            else break;
        }
        return v;
    }
    MVal eval_eq() {
        MVal v = eval_rel();
        for (;;) {
            if (peek_op("==")) { eat_op("=="); MVal r = eval_rel(); v.t = 0; v.num = (v.num == r.num); }
            else if (peek_op("!=")) { eat_op("!="); MVal r = eval_rel(); v.t = 0; v.num = (v.num != r.num); }
            else break;
        }
        return v;
    }
    MVal eval_and() {
        MVal v = eval_eq();
        for (;;) {
            if (peek_op("&&")) {
                eat_op("&&");
                MVal r = eval_eq();
                v.t = 0;
                v.num = (v.num != 0) && (r.num != 0);
            } else break;
        }
        return v;
    }
    MVal eval_or() {
        MVal v = eval_and();
        for (;;) {
            if (peek_op("||")) {
                eat_op("||");
                MVal r = eval_and();
                v.t = 0;
                v.num = (v.num != 0) || (r.num != 0);
            } else break;
        }
        return v;
    }

    // assignment: ident = expr | var ident = expr | let ident = expr
    bool try_assign(bool with_decl) {
        skip_ws();
        const char* save = p;
        if (!is_ident_start(*p)) return false;
        char name[20]; int n = 0;
        while (is_ident(*p) && n < 18) name[n++] = *p++;
        name[n] = 0;
        skip_ws();
        if (!peek_op("=")) { p = save; return false; }
        eat_op("=");
        MVal* v = with_decl ? declare_var(name) : find_var(name);
        if (!v) { err = 1; return true; }
        MVal r = eval_or();
        *v = r;
        return true;
    }

    void exec_stmt() {
        skip_ws();
        if (err) return;
        if (*p == 0) return;
        // block
        if (peek_op("{")) {
            eat_op("{");
            for (;;) {
                skip_ws();
                if (err || *p == 0) break;
                if (peek_op("}")) { eat_op("}"); break; }
                exec_stmt();
                skip_ws();
                if (peek_op(";")) eat_op(";");
            }
            return;
        }
        // if
        if (peek_op("if")) {
            eat_op("if");
            skip_ws();
            if (peek_op("(")) eat_op("(");
            MVal c = eval_or();
            skip_ws();
            if (peek_op(")")) eat_op(")");
            bool taken = false;
            if (c.num != 0) { exec_stmt(); taken = true; }
            else { skip_past_stmt(); }
            skip_ws();
            if (peek_op("else")) {
                eat_op("else");
                if (!taken) exec_stmt();
                else skip_past_stmt();
            }
            return;
        }
        // while
        if (peek_op("while")) {
            eat_op("while");
            skip_ws();
            if (peek_op("(")) eat_op("(");
            const char* cond_start = p;
            int guard = 0;
            for (;;) {
                p = cond_start;
                MVal c = eval_or();
                skip_ws();
                if (peek_op(")")) eat_op(")");
                if (c.num == 0 || err) {
                    skip_past_stmt();   // consume the untaken body
                    break;
                }
                exec_stmt();
                if (++guard > 20000) { err = 1; break; }
            }
            return;
        }
        // for
        if (peek_op("for")) {
            eat_op("for");
            skip_ws();
            if (peek_op("(")) eat_op("(");
            // init
            skip_ws();
            if (peek_op("var") || peek_op("let")) {
                eat_op(peek_op("var") ? "var" : "let");
                try_assign(true);
            } else {
                try_assign(false);
            }
            skip_ws();
            if (peek_op(";")) eat_op(";");
            const char* cond_start = p;
            // locate inc: after the first ';' following the condition
            const char* q = cond_start;
            const char* inc_start = 0;
            while (*q) {
                if (*q == ';') { inc_start = q + 1; break; }
                if (*q == ')') break;
                q++;
            }
            int guard = 0;
            for (;;) {
                p = cond_start;
                MVal c; set_num(c, 1);
                skip_ws();
                if (!peek_op(";")) c = eval_or();
                skip_ws();
                if (peek_op(";")) eat_op(";");
                while (*p && *p != ')') p++;   // skip inc clause
                if (peek_op(")")) eat_op(")");
                if (c.num == 0 || err) {
                    skip_past_stmt();   // consume the untaken body
                    break;
                }
                exec_stmt();
                if (++guard > 20000) { err = 1; break; }
                // inc
                p = inc_start;
                skip_ws();
                if (!peek_op(")")) {
                    char name[20]; int n = 0;
                    while (is_ident(*p) && n < 18) name[n++] = *p++;
                    name[n] = 0;
                    skip_ws();
                    if (peek_op("=")) {
                        eat_op("=");
                        MVal* v = find_var(name);
                        MVal r = eval_or();
                        if (v) *v = r;
                    } else if (peek_op("++")) {
                        eat_op("++");
                        MVal* v = find_var(name);
                        if (v) v->num++;
                    } else if (peek_op("--")) {
                        eat_op("--");
                        MVal* v = find_var(name);
                        if (v) v->num--;
                    }
                }
            }
            return;
        }
        // var / let declaration
        if (peek_op("var") || peek_op("let")) {
            eat_op(peek_op("var") ? "var" : "let");
            try_assign(true);
            return;
        }
        // plain assignment
        if (try_assign(false)) return;
        // bare expression (call)
        eval_or();
    }

    // skip one statement (for the untaken if/else branch)
    void skip_past_stmt() {
        skip_ws();
        if (peek_op("{")) {
            int depth = 0;
            for (;;) {
                if (*p == 0) break;
                if (*p == '{') depth++;
                if (*p == '}') { depth--; if (depth == 0) { p++; break; } }
                p++;
            }
            return;
        }
        while (*p && *p != ';') {
            if (peek_op("{")) { skip_past_stmt(); continue; }
            p++;
        }
        if (*p == ';') p++;
    }

    void run() {
        skip_ws();
        while (*p && !err) {
            exec_stmt();
            skip_ws();
            if (peek_op(";")) eat_op(";");
        }
        out[oi] = 0;
    }
};

} // namespace

int mini_js_run(const char* script, char* out, int out_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    MJs js;
    js.p = script;
    js.out = out;
    js.out_sz = out_sz;
    js.oi = 0;
    js.err = 0;
    js.nvars = 0;
    out[0] = 0;
    js.run();
    return js.err;
}

} // namespace nefu
