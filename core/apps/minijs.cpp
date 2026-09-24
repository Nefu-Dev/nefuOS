// nefuOS mini JS engine - minimal embeddable JavaScript-like interpreter.
// Numbers are integers (kernel has no FPU path), strings are fixed buffers.
// Parser structure inspired by "Let's Build a Simple Interpreter" (R. Spivak,
// MIT) and tiny-js (MIT); implementation is original, kernel-friendly (no
// dynamic allocation). Powers the browser <script> blocks and terminal `js`.
#include "minijs.h"
#include "../klib/klib.h"

#ifdef NEFU_BARE
// nefuOS kernel build: the self-contained toy interpreter below (no libc).
// The Windows host build (below) uses QuickJS (bellard/quickjs, MIT) instead.
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
            // DOM commands for the browser host: setText(id, txt) /
            // setColor(id, #hex) / hide(id) / show(id). Each call is encoded
            // as a "\x01cmd|id|value" control line in the output stream; the
            // browser parses those lines and mutates its DOM, then re-layouts.
            if (strcmp(name, "setText") == 0 || strcmp(name, "setColor") == 0 ||
                strcmp(name, "hide") == 0 || strcmp(name, "show") == 0) {
                if (*p == '(') {
                    p++;
                    MVal idv; set_num(idv, 0);
                    skip_ws();
                    if (*p != ')') idv = eval_or();
                    skip_ws();
                    MVal valv; set_num(valv, 0);
                    if (*p == ',') { p++; skip_ws(); }
                    if (*p != ')') valv = eval_or();
                    skip_ws();
                    if (*p == ')') p++;
                    emit('\x01');
                    emit_str(name);
                    emit('|');
                    if (idv.t == 1) emit_str(idv.str);
                    else { char nb[16]; ksprintf(nb, sizeof(nb), "%d", idv.num); emit_str(nb); }
                    emit('|');
                    if (valv.t == 1) emit_str(valv.str);
                    else { char nb[16]; ksprintf(nb, sizeof(nb), "%d", valv.num); emit_str(nb); }
                    emit('\n');
                    return v;
                }
                err = 1;
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

#else  // !NEFU_BARE: host build uses QuickJS (bellard/quickjs, MIT).

#include "quickjs/quickjs.h"
#include <string.h>
#include <stdlib.h>

namespace nefu {

namespace {

// out-stream writer state shared with the JS callbacks below
struct RunCtx { char* out; int out_sz; int oi; };

static void ctx_emit(RunCtx* rc, const char* s) {
    while (*s && rc->oi < rc->out_sz - 1) rc->out[rc->oi++] = *s++;
}
static void ctx_emitc(RunCtx* rc, char c) {
    if (rc->oi < rc->out_sz - 1) rc->out[rc->oi++] = c;
}

// print/alert -> out + newline; document.write -> out only (magic 0/1)
static JSValue nfu_out(JSContext* c, JSValueConst thisv, int argc,
                       JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    if (argc > 0) {
        const char* s = JS_ToCString(c, argv[0]);
        if (s) { ctx_emit(rc, s); JS_FreeCString(c, s); }
    }
    if (magic == 1) ctx_emitc(rc, '\n');
    return JS_UNDEFINED;
}

// setText/setColor/hide/show -> "\x01cmd|id|val\n" control line for the browser
static JSValue nfu_domcmd(JSContext* c, JSValueConst thisv, int argc,
                          JSValueConst* argv, int magic) {
    static const char* const names[4] = { "setText", "setColor", "hide", "show" };
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    const char* idv = argc > 0 ? JS_ToCString(c, argv[0]) : 0;
    const char* val = (argc > 1) ? JS_ToCString(c, argv[1]) : 0;
    ctx_emitc(rc, '\x01');
    ctx_emit(rc, names[magic]);
    ctx_emitc(rc, '|');
    if (idv) { ctx_emit(rc, idv); JS_FreeCString(c, idv); }
    ctx_emitc(rc, '|');
    if (val) { ctx_emit(rc, val); JS_FreeCString(c, val); }
    ctx_emitc(rc, '\n');
    return JS_UNDEFINED;
}

static JSValue nfu_len(JSContext* c, JSValueConst thisv, int argc,
                       JSValueConst* argv) {
    if (argc > 0) {
        const char* s = JS_ToCString(c, argv[0]);
        if (s) { int n = (int)strlen(s); JS_FreeCString(c, s); return JS_NewInt32(c, n); }
    }
    return JS_NewInt32(c, 0);
}

// ===================== WebGL bridge =====================
// document.getElementById(id) -> canvas object -> getContext('webgl2') -> a gl
// object. Every GL call is encoded as a "\x01gl<cmd>|cid|args" control line
// (args URL percent-encoded, comma-separated) that browser.cpp's gl_cmd()
// decodes and executes on the hidden WGL context / iGPU.
static JSClassID s_canvas_cls, s_gl_cls;
static int s_sh_n = 0, s_pr_n = 0, s_bu_n = 0;   // JS-side object ids

static void canvas_finalizer(JSRuntime* rt, JSValue val) {
    void* p = JS_GetOpaque(val, s_canvas_cls);
    if (p) free(p);
}
static void gl_finalizer(JSRuntime* rt, JSValue val) {
    void* p = JS_GetOpaque(val, s_gl_cls);
    if (p) free(p);
}

static void urlenc(const char* in, char* out, int cap) {
    static const char* hexd = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char* u = (const unsigned char*)in; *u && n < cap - 4; u++) {
        unsigned char ch = *u;
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == '_') {
            out[n++] = (char)ch;
        } else {
            out[n++] = '%'; out[n++] = hexd[ch >> 4]; out[n++] = hexd[ch & 15];
        }
    }
    out[n] = 0;
}

static void gl_emit(RunCtx* rc, const char* sub, const char* cid, const char* args) {
    char enc[1600];
    urlenc(args, enc, sizeof(enc));
    ctx_emitc(rc, '\x01');
    ctx_emit(rc, "gl"); ctx_emit(rc, sub);
    ctx_emitc(rc, '|'); ctx_emit(rc, cid);
    ctx_emitc(rc, '|'); ctx_emit(rc, enc);
    ctx_emitc(rc, '\n');
}

static char* gl_cid_of(JSContext* c, JSValueConst thisv) {
    if (!JS_IsObject(thisv)) return 0;
    return (char*)JS_GetOpaque(thisv, s_gl_cls);
}
static char* canvas_cid_of(JSContext* c, JSValueConst thisv) {
    if (!JS_IsObject(thisv)) return 0;
    return (char*)JS_GetOpaque(thisv, s_canvas_cls);
}

// numeric GL commands (magic = index into names)
static const char* glcmd_names[] = {
    "glc", "glclr", "v", "csh", "link", "use", "draw", "att",
    "bindbuf", "attptr"
};
static JSValue nfu_gl(JSContext* c, JSValueConst thisv, int argc,
                      JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    char args[512]; int ap = 0;
    if (magic == 0) {   // clearColor: exactly 4 floats
        double f[4] = {0, 0, 0, 1};
        int n = argc > 4 ? 4 : argc;
        for (int i = 0; i < n; i++) JS_ToFloat64(c, &f[i], argv[i]);
        ap = sprintf(args, "%g,%g,%g,%g", f[0], f[1], f[2], f[3]);
    } else if (magic == 1) {   // clear: 1 int
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", (int)d);
    } else if (magic == 2) {   // viewport: 4 ints
        int v[4] = {0, 0, 300, 150};
        int n = argc > 4 ? 4 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
    } else if (magic == 3 || magic == 4 || magic == 5) {   // compileShader/link/use: 1 id
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", (int)d);
    } else if (magic == 6) {   // drawArrays(mode, first, count)
        int v[3] = {0, 0, 0};
        int n = argc > 3 ? 3 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[0], v[2]);
    } else if (magic == 7) {   // attachShader(pid, sid)
        int v[2] = {0, 0};
        int n = argc > 2 ? 2 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[0], v[1]);
    } else if (magic == 8) {   // bindBuffer(target, bid)
        int v[2] = {0, 0};
        int n = argc > 2 ? 2 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[1], v[0]);
    } else if (magic == 9) {   // vertexAttribPointer(loc,size,type,norm,stride,offset)
        int v[4] = {0, 0, 0, 0};
        double d;
        if (argc > 0) { JS_ToFloat64(c, &d, argv[0]); v[0] = (int)d; }
        if (argc > 1) { JS_ToFloat64(c, &d, argv[1]); v[1] = (int)d; }
        if (argc > 4) { JS_ToFloat64(c, &d, argv[4]); v[2] = (int)d; }
        if (argc > 5) { JS_ToFloat64(c, &d, argv[5]); v[3] = (int)d; }
        ap = sprintf(args, "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
    }
    if (magic == 10) return JS_UNDEFINED;   // enableVertexAttribArray: done by glattptr
    args[ap] = 0;
    gl_emit(rc, glcmd_names[magic], cid, args);
    return JS_UNDEFINED;
}

// createShader(type) -> sid; type remembered for shaderSource
static int s_sh_type[256];
static JSValue nfu_createShader(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, 0);
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    int sid = s_sh_n + 1; if (sid > 255) sid = 255;
    s_sh_n = sid;
    s_sh_type[sid] = (int)d;
    return JS_NewInt32(c, sid);
}

// shaderSource(sid, src)
static JSValue nfu_shaderSource(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    int sid = (int)d;
    const char* src = argc > 1 ? JS_ToCString(c, argv[1]) : 0;
    if (!src) return JS_UNDEFINED;
    char args[1400];
    int ap = sprintf(args, "%d,%d,", sid, s_sh_type[sid]);
    int sl = (int)strlen(src);
    if (ap + sl >= (int)sizeof(args) - 1) sl = (int)sizeof(args) - 1 - ap;
    memcpy(args + ap, src, (size_t)sl);
    args[ap + sl] = 0;
    JS_FreeCString(c, src);
    gl_emit(rc, "sh", cid, args);
    return JS_UNDEFINED;
}

static JSValue nfu_createProgram(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, 0);
    int pid = s_pr_n + 1; if (pid > 255) pid = 255;
    s_pr_n = pid;
    char args[64];
    sprintf(args, "%d", pid);
    gl_emit(rc, "prog", cid, args);
    return JS_NewInt32(c, pid);
}

static JSValue nfu_createBuffer(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, 0);
    int bid = s_bu_n + 1; if (bid > 255) bid = 255;
    s_bu_n = bid;
    return JS_NewInt32(c, bid);
}

// bindBuffer(target, bid) - remember target for bufferData
static int s_bu_target[256];
static JSValue nfu_bindBuffer(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    double td = 0, bd = 0;
    if (argc > 0) JS_ToFloat64(c, &td, argv[0]);
    if (argc > 1) JS_ToFloat64(c, &bd, argv[1]);
    int bid = (int)bd;
    if (bid >= 0 && bid < 256) s_bu_target[bid] = (int)td;
    char args[64];
    sprintf(args, "%d,%d", bid, (int)td);
    gl_emit(rc, "bindbuf", cid, args);
    return JS_UNDEFINED;
}

// bufferData(target, data, usage) - data may be a float array or a
// comma-separated string; both become "bid,target,f1,f2,..." in the command
static JSValue nfu_bufferData(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    double td = 0; if (argc > 0) JS_ToFloat64(c, &td, argv[0]);
    int bid = 0;
    for (int i = 1; i < 256; i++) if (s_bu_target[i] == (int)td) { bid = i; break; }
    char data[1200];
    data[0] = 0;
    int dn = 0;
    if (argc > 1 && JS_IsArray(c, argv[1])) {
        for (int i = 0; i < 512 && dn < 1100; i++) {
            JSValue v = JS_GetPropertyUint32(c, argv[1], (uint32_t)i);
            int undef = JS_IsUndefined(v);
            if (undef) { JS_FreeValue(c, v); break; }
            double d = 0;
            int bad = JS_ToFloat64(c, &d, v);
            JS_FreeValue(c, v);
            if (bad) break;
            int tl = sprintf(data + dn, i == 0 ? "%g" : ",%g", d);
            dn += tl;
        }
    } else {
        const char* ds = argc > 1 ? JS_ToCString(c, argv[1]) : 0;
        if (ds) {
            int sl = (int)strlen(ds);
            if (sl > 1100) sl = 1100;
            memcpy(data, ds, (size_t)sl);
            data[sl] = 0;
            JS_FreeCString(c, ds);
        }
    }
    if (!data[0]) return JS_UNDEFINED;
    char args[1600];
    sprintf(args, "%d,%d,%s", bid, (int)td, data);
    gl_emit(rc, "buf", cid, args);
    return JS_UNDEFINED;
}

// getContext('webgl'|'webgl2') -> gl object (emits glctx to create the context)
static JSValue nfu_getContext(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = canvas_cid_of(c, thisv);
    if (!cid) return JS_NULL;
    gl_emit(rc, "ctx", cid, "");
    JSValue g = JS_NewObjectClass(c, s_gl_cls);
    char* op = (char*)malloc(strlen(cid) + 1);
    if (op) memcpy(op, cid, strlen(cid) + 1);
    JS_SetOpaque(g, op);
    JS_SetPropertyStr(c, g, "clearColor", JS_NewCFunctionMagic(c, nfu_gl, "clearColor", 4, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(c, g, "clear", JS_NewCFunctionMagic(c, nfu_gl, "clear", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "viewport", JS_NewCFunctionMagic(c, nfu_gl, "viewport", 4, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "compileShader", JS_NewCFunctionMagic(c, nfu_gl, "compileShader", 1, JS_CFUNC_generic_magic, 3));
    JS_SetPropertyStr(c, g, "linkProgram", JS_NewCFunctionMagic(c, nfu_gl, "linkProgram", 1, JS_CFUNC_generic_magic, 4));
    JS_SetPropertyStr(c, g, "useProgram", JS_NewCFunctionMagic(c, nfu_gl, "useProgram", 1, JS_CFUNC_generic_magic, 5));
    JS_SetPropertyStr(c, g, "drawArrays", JS_NewCFunctionMagic(c, nfu_gl, "drawArrays", 3, JS_CFUNC_generic_magic, 6));
    JS_SetPropertyStr(c, g, "attachShader", JS_NewCFunctionMagic(c, nfu_gl, "attachShader", 2, JS_CFUNC_generic_magic, 7));
    JS_SetPropertyStr(c, g, "vertexAttribPointer", JS_NewCFunctionMagic(c, nfu_gl, "vertexAttribPointer", 6, JS_CFUNC_generic_magic, 9));
    JS_SetPropertyStr(c, g, "enableVertexAttribArray", JS_NewCFunctionMagic(c, nfu_gl, "enableVertexAttribArray", 1, JS_CFUNC_generic_magic, 10));
    JS_SetPropertyStr(c, g, "createShader", JS_NewCFunction(c, nfu_createShader, "createShader", 1));
    JS_SetPropertyStr(c, g, "shaderSource", JS_NewCFunction(c, nfu_shaderSource, "shaderSource", 2));
    JS_SetPropertyStr(c, g, "createProgram", JS_NewCFunction(c, nfu_createProgram, "createProgram", 0));
    JS_SetPropertyStr(c, g, "createBuffer", JS_NewCFunction(c, nfu_createBuffer, "createBuffer", 0));
    JS_SetPropertyStr(c, g, "bindBuffer", JS_NewCFunction(c, nfu_bindBuffer, "bindBuffer", 2));
    JS_SetPropertyStr(c, g, "bufferData", JS_NewCFunction(c, nfu_bufferData, "bufferData", 3));
    // GL constants (WebGL2 / GL ES)
    JS_SetPropertyStr(c, g, "VERTEX_SHADER", JS_NewInt32(c, 0x8B31));
    JS_SetPropertyStr(c, g, "FRAGMENT_SHADER", JS_NewInt32(c, 0x8B30));
    JS_SetPropertyStr(c, g, "ARRAY_BUFFER", JS_NewInt32(c, 0x8892));
    JS_SetPropertyStr(c, g, "ELEMENT_ARRAY_BUFFER", JS_NewInt32(c, 0x8893));
    JS_SetPropertyStr(c, g, "STATIC_DRAW", JS_NewInt32(c, 0x88E4));
    JS_SetPropertyStr(c, g, "TRIANGLES", JS_NewInt32(c, 0x0004));
    JS_SetPropertyStr(c, g, "COLOR_BUFFER_BIT", JS_NewInt32(c, 0x4000));
    JS_SetPropertyStr(c, g, "DEPTH_BUFFER_BIT", JS_NewInt32(c, 0x0100));
    JS_SetPropertyStr(c, g, "FLOAT", JS_NewInt32(c, 0x1406));
    JS_SetPropertyStr(c, g, "FALSE", JS_NewInt32(c, 0));
    JS_SetPropertyStr(c, g, "TRUE", JS_NewInt32(c, 1));
    return g;
}

// document.getElementById(id) -> canvas wrapper
static JSValue nfu_getElementById(JSContext* c, JSValueConst thisv, int argc,
                                  JSValueConst* argv) {
    const char* idv = argc > 0 ? JS_ToCString(c, argv[0]) : 0;
    JSValue obj = JS_NewObjectClass(c, s_canvas_cls);
    char* op = 0;
    if (idv) {
        op = (char*)malloc(strlen(idv) + 1);
        if (op) memcpy(op, idv, strlen(idv) + 1);
    }
    JS_SetOpaque(obj, op);
    JS_SetPropertyStr(c, obj, "getContext",
        JS_NewCFunctionMagic(c, nfu_getContext, "getContext", 1, JS_CFUNC_generic_magic, 0));
    if (idv) JS_FreeCString(c, idv);
    return obj;
}

// one shared runtime+context: global state survives across <script> blocks,
// exactly like a real browser
static JSRuntime* s_rt;
static JSContext* s_ctx;

static void ensure_js() {
    if (s_rt) return;
    s_rt = JS_NewRuntime();
    if (!s_rt) return;
    JS_SetMemoryLimit(s_rt, 16 * 1024 * 1024);   // cap runaway scripts
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = 0; return; }
    JSValue g = JS_GetGlobalObject(s_ctx);
    JS_SetPropertyStr(s_ctx, g, "print",
        JS_NewCFunctionMagic(s_ctx, nfu_out, "print", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(s_ctx, g, "alert",
        JS_NewCFunctionMagic(s_ctx, nfu_out, "alert", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(s_ctx, g, "len",
        JS_NewCFunction(s_ctx, nfu_len, "len", 1));
    JS_SetPropertyStr(s_ctx, g, "setText",
        JS_NewCFunctionMagic(s_ctx, nfu_domcmd, "setText", 2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(s_ctx, g, "setColor",
        JS_NewCFunctionMagic(s_ctx, nfu_domcmd, "setColor", 2, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(s_ctx, g, "hide",
        JS_NewCFunctionMagic(s_ctx, nfu_domcmd, "hide", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(s_ctx, g, "show",
        JS_NewCFunctionMagic(s_ctx, nfu_domcmd, "show", 1, JS_CFUNC_generic_magic, 3));
    JS_NewClassID(&s_canvas_cls);
    JSClassDef cdef;
    memset(&cdef, 0, sizeof(cdef));
    cdef.class_name = "NfuCanvas";
    cdef.finalizer = canvas_finalizer;
    JS_NewClass(s_rt, s_canvas_cls, &cdef);
    JS_NewClassID(&s_gl_cls);
    memset(&cdef, 0, sizeof(cdef));
    cdef.class_name = "NfuGL";
    cdef.finalizer = gl_finalizer;
    JS_NewClass(s_rt, s_gl_cls, &cdef);
    JSValue doc = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, doc, "write",
        JS_NewCFunctionMagic(s_ctx, nfu_out, "write", 1, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(s_ctx, doc, "getElementById",
        JS_NewCFunction(s_ctx, nfu_getElementById, "getElementById", 1));
    JS_SetPropertyStr(s_ctx, g, "document", doc);
    // keep the doc JSValue alive: freeing it here would drop the reference
    // below the one held by the global object and break document.write
    JS_FreeValue(s_ctx, g);
}

} // namespace

int mini_js_run(const char* script, char* out, int out_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    out[0] = 0;
    ensure_js();
    if (!s_ctx) return 1;
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    JS_SetContextOpaque(s_ctx, &rc);
    JSValue r = JS_Eval(s_ctx, script, (size_t)strlen(script), "<script>",
                        JS_EVAL_TYPE_GLOBAL);
    int err = 0;
    if (JS_IsException(r)) {
        err = 1;
        JSValue ex = JS_GetException(s_ctx);
        const char* msg = JS_ToCString(s_ctx, ex);
        if (msg) {
            ctx_emit(&rc, "E: ");
            ctx_emit(&rc, msg);
            JS_FreeCString(s_ctx, msg);
        }
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    return err;
}

} // namespace nefu
#endif
