// nefuOS mini JS engine - minimal embeddable JavaScript-like interpreter.
// Numbers are integers (kernel has no FPU path), strings are fixed buffers.
// Parser structure inspired by "Let's Build a Simple Interpreter" (R. Spivak,
// MIT) and tiny-js (MIT); implementation is original, kernel-friendly (no
// dynamic allocation). Powers the browser <script> blocks and terminal `js`.
#include "minijs.h"
#include "jsdom_shim.h"
#include "../klib/klib.h"

#if 0
// nefuOS bare build: QuickJS (bellard/quickjs, MIT) is the only JS engine now.
// The self-contained toy interpreter below is retired (kept as reference only).
// The Windows host build (below) uses QuickJS too.
namespace nefu {
#ifndef NEFU_BARE
// defined in browser.cpp (Win32 WGL path); direct bridge calls for the JS engine
void gl_ctx_create(const char* cid, int w, int h);
void gl_tex_upload(const char* cid, int target, int level, int internal,
                   int w, int h, int format, int type,
                   const uint8_t* px, int len);
#endif

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
    char* html;      // DOM re-sync buffer (final serialized tree)
    int html_sz;
    int hi;
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

// browser environment: latest URL/title/viewport seen by scripts
static const char* s_env_url = 0;
static const char* s_env_title = 0;
static int s_env_w = 0, s_env_h = 0;

void mini_js_set_env(const char* url, const char* title, int w, int h) {
    s_env_url = url;
    s_env_title = title;
    s_env_w = w; s_env_h = h;
}

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
#include "../platform.h"
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace nefu {
#ifndef NEFU_BARE
// defined in browser.cpp (Win32 WGL path); direct bridge calls for the JS engine
void gl_ctx_create(const char* cid, int w, int h);
#endif
// defined in browser.cpp: host = WGL direct texture upload, bare = no-op stub
void gl_tex_upload(const char* cid, int target, int level, int internal,
                   int w, int h, int format, int type,
                   const uint8_t* px, int len);

// browser environment: latest URL/title/viewport seen by scripts.
// Declared BEFORE the anonymous namespace: ensure_js() (inside it) uses them,
// and namespace-scope lookup respects declaration order.
static const char* s_env_url = 0;
static const char* s_env_title = 0;
static const char* s_env_dom = 0;
static int s_env_w = 0, s_env_h = 0;

void mini_js_set_env(const char* url, const char* title, int w, int h) {
    s_env_url = url;
    s_env_title = title;
    s_env_w = w; s_env_h = h;
}
void mini_js_set_dom(const char* html) {
    s_env_dom = html;   // read by ensure_js when the QuickJS runtime is born
}

namespace {

// out-stream writer state shared with the JS callbacks below
struct RunCtx { char* out; int out_sz; int oi; uint32_t deadline;
             char* html; int html_sz; int hi;
             volatile const char* last_c; volatile uint32_t last_c_t;
             volatile int watch_done; volatile DWORD jtid;
             volatile unsigned long long exe_base; volatile int rip_printed; volatile uint32_t rip_t;
             };

// trace which native (C) call the JS thread last entered, for the watchdog
static void nfu_trace(RunCtx* rc, const char* what) {
    if (!rc) return;
    rc->last_c = what;
    rc->last_c_t = platform_tick_ms();
}

// kill runaway scripts after the 1s budget: JS_SetInterruptHandler fires
// between bytecode instructions, so this stops infinite loops cleanly
static int js_interrupt_cb(JSRuntime* rt, void* opaque) {
    RunCtx* rc = (RunCtx*)opaque;
    if (!rc) return 0;
#ifndef NEFU_BARE
    static uint32_t last_t = 0;
    uint32_t now = platform_tick_ms();
    if (now - last_t > 1000) {
        last_t = now;
        rc->last_c_t = now;   // progress marker for the watchdog
        JSMemoryUsage mu;
        JS_ComputeMemoryUsage(rt, &mu);
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "\njs: t=%u dl=%u mem=%lld\n", now, rc->deadline, (long long)mu.memory_used_size);
        if (n > 0) { fwrite(buf, 1, (size_t)n, stdout); fflush(stdout); }
    }
    return now > rc->deadline ? 1 : 0;
#else
    return platform_tick_ms() > rc->deadline ? 1 : 0;
#endif
}

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
    nfu_trace(rc, "out");
    // Only stringify real strings: JS_ToCString on exotic objects (opaque
    // stubs, arraybuffers) can stall inside quickjs's C ToString, same as
    // JSON.parse(obj). Real browsers print "[object X]" instantly; we skip.
    if (argc > 0 && JS_IsString(argv[0])) {
        const char* s = JS_ToCString(c, argv[0]);
        if (s) { ctx_emit(rc, s); JS_FreeCString(c, s); }
    }
    if (magic == 1) ctx_emitc(rc, '\n');
#ifndef NEFU_BARE
    // probe: rate-limited console.log trace to stderr (locate where bundle stalls)
    {
        static uint32_t last_log_t = 0; static unsigned log_n = 0;
        uint32_t now = platform_tick_ms();
        log_n++;
        const char* mark = 0;
        if (argc > 0 && JS_IsString(argv[0])) mark = JS_ToCString(c, argv[0]);
        int urgent = (mark && strstr(mark, "[MARK]")) ? 1 : 0;
        if (now - last_log_t >= 500 || urgent) {
            char lb[96];
            int ln = 0;
            if (mark) { ln = snprintf(lb, sizeof(lb), "log#%u t=%u: %.60s", log_n, now, mark); }
            else ln = snprintf(lb, sizeof(lb), "log#%u t=%u: (noarg)", log_n, now);
            if (ln > 0) { fwrite(lb, 1, (size_t)ln, stdout); fputc('\n', stdout); fflush(stdout); }
            last_log_t = now;
        }
        if (mark) JS_FreeCString(c, mark);
    }
#endif
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
    if (argc > 0 && JS_IsString(argv[0])) {
        size_t L = 0;
        const char* s = JS_ToCStringLen(c, &L, argv[0]);
        if (s) { JS_FreeCString(c, s); return JS_NewInt32(c, (int)L); }
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
static int s_u_n = 0, s_a_n = 0, s_va_n = 0, s_tx_n = 0;   // uniform/attrib loc, VAO, texture ids

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

// Append up to maxn numbers from a typed array (Float32Array/Uint16Array/...)
// or a plain JS array to a ",%g" comma list written at out+ap. Returns the
// element count. Typed arrays are read through their backing ArrayBuffer so
// `new Float32Array([...])` vertex data works like in a real browser.
static int append_nums(JSContext* c, JSValueConst v, char* out, int ap, int cap, int maxn) {
    int got = 0;
    size_t off = 0, blen = 0, bpe = 1;
    JSValue ab = JS_GetTypedArrayBuffer(c, v, &off, &blen, &bpe);
    if (JS_IsException(ab)) {
        JS_FreeValue(c, JS_GetException(c));   // not a typed array: clear the error
    } else {
        size_t absz = 0;
        uint8_t* base = JS_GetArrayBuffer(c, &absz, ab);
        JS_FreeValue(c, ab);
        if (base && bpe && off + blen <= absz) {
            size_t cnt = blen / bpe;
            for (size_t i = 0; i < cnt && got < maxn; i++) {
                double d = 0;
                const uint8_t* p = base + off + i * bpe;
                if (bpe == 4) { float fv; memcpy(&fv, p, 4); d = fv; }
                else if (bpe == 2) { uint16_t u; memcpy(&u, p, 2); d = u; }
                else if (bpe == 1) d = *p;
                else if (bpe == 8) { double dv; memcpy(&dv, p, 8); d = dv; }
                else break;
                if (ap + 32 >= cap) break;
                ap += sprintf(out + ap, ",%g", d);
                got++;
            }
            return got;
        }
    }
    if (JS_IsArray(c, v)) {
        for (int i = 0; i < maxn && ap + 32 < cap; i++) {
            JSValue e = JS_GetPropertyUint32(c, v, (uint32_t)i);
            int undef = JS_IsUndefined(e);
            if (undef) { JS_FreeValue(c, e); break; }
            double d = 0;
            int bad = JS_ToFloat64(c, &d, e);
            JS_FreeValue(c, e);
            if (bad) break;
            ap += sprintf(out + ap, ",%g", d);
            got++;
        }
    }
    return got;
}

// numeric GL commands (magic = index into names)
static const char* glcmd_names[] = {
    "glc", "glclr", "v", "csh", "link", "use", "draw", "att",
    "bindbuf", "attptr",
    // magic 10 (enableVertexAttribArray) never emits: glattptr does both.
    // This slot only keeps the table aligned with the magic numbers below.
    "-",
    // WebGL2 bridge (numeric args)
    "u1f", "u2f", "u3f", "u4f", "u1i", "u2i", "u3i", "u4i",
    "deli", "en", "dis", "blend", "dfunc", "cull", "actex", "texp", "texim", "btx",
    "bva"
};
static JSValue nfu_gl(JSContext* c, JSValueConst thisv, int argc,
                      JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    nfu_trace(rc, "gl");
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    char args[1600]; int ap = 0;
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
    } else if (magic >= 11 && magic <= 18) {   // uniform1f..4f / uniform1i..4i (loc, vals)
        int nvals = 1 + (magic - 11) % 4;
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", (int)d);
        for (int i = 0; i < nvals; i++) {
            double fv = 0; if (argc > i + 1) JS_ToFloat64(c, &fv, argv[i + 1]);
            ap += sprintf(args + ap, ",%g", fv);
        }
    } else if (magic == 19) {   // drawElements(mode, count, type, offset)
        int v[4] = {0, 0, 0, 0};
        int n = argc > 4 ? 4 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
    } else if (magic == 20 || magic == 21) {   // enable/disable(cap)
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", (int)d);
    } else if (magic == 22) {   // blendFunc(sf, df)
        int v[2] = {0, 0};
        int n = argc > 2 ? 2 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[0], v[1]);
    } else if (magic == 23 || magic == 24 || magic == 25 || magic == 29) {   // depthFunc/cullFace/activeTexture/bindVertexArray
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", (int)d);
    } else if (magic == 26) {   // texParameteri(target, pname, param)
        int v[3] = {0, 0, 0};
        int n = argc > 3 ? 3 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d,%d", v[0], v[1], v[2]);
    } else if (magic == 27) {   // texImage2D(target,level,internal,w,h,border,format,type[,pixels])
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        int n = argc > 8 ? 8 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        // pixel data as a typed array / ArrayBuffer: the worker thread only
        // copies the bytes into the canvas' pending slot (no GL); the UI
        // thread applies them when the texim command runs, so all GL calls
        // stay on one thread.
        if (argc > 8 && !JS_IsUndefined(argv[8]) && JS_IsObject(argv[8])) {
            size_t off2 = 0, blen2 = 0, bpe2 = 1;
            JSValue ab2 = JS_GetTypedArrayBuffer(c, argv[8], &off2, &blen2, &bpe2);
            if (!JS_IsException(ab2)) {
                size_t absz2 = 0;
                uint8_t* base2 = JS_GetArrayBuffer(c, &absz2, ab2);
                if (base2 && off2 + blen2 <= absz2 && blen2 > 0) {
                    uint8_t* px = base2 + off2;
                    gl_tex_upload(cid, v[0], v[1], v[2], v[3], v[4],
                                  v[6], v[7], px, (int)blen2);
                }
                JS_FreeValue(c, ab2);
            } else {
                JS_FreeValue(c, JS_GetException(c));
            }
        }
        ap = sprintf(args, "%d,%d,%d,%d,%d,%d,%d,%d", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    } else if (magic == 28) {   // bindTexture(target, tex)
        int v[2] = {0, 0};
        int n = argc > 2 ? 2 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[0], v[1]);
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
    size_t sl = 0;
    const char* src = (argc > 1 && JS_IsString(argv[1])) ? JS_ToCStringLen(c, &sl, argv[1]) : 0;
    if (!src) return JS_UNDEFINED;
    char args[1400];
    int ap = sprintf(args, "%d,%d,", sid, s_sh_type[sid]);
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
    if (argc > 1 && JS_IsObject(argv[1])) {
        // Float32Array/Uint16Array/... vertex or index data, or a plain array
        dn = append_nums(c, argv[1], data, 0, (int)sizeof(data), 160);
    }
    if (!dn) {
        size_t sl = 0;
        const char* ds = (argc > 1 && JS_IsString(argv[1])) ? JS_ToCStringLen(c, &sl, argv[1]) : 0;
        if (ds) {
            if (sl > 1100) sl = 1100;
            data[0] = ',';   // keep the ",%g"-style list layout
            memcpy(data + 1, ds, sl);
            data[1 + sl] = 0;
            JS_FreeCString(c, ds);
        }
    }
    if (!data[0]) return JS_UNDEFINED;
    char args[1600];
    sprintf(args, "%d,%d%s", bid, (int)td, data);
    gl_emit(rc, "buf", cid, args);
    return JS_UNDEFINED;
}

// getUniformLocation(pid, name) -> proxy uniform id; the host resolves it to a
// real GL location and remembers the mapping (the protocol is one-way).
static JSValue nfu_getUniformLocation(JSContext* c, JSValueConst thisv, int argc,
                                      JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, -1);
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    int uid = s_u_n + 1; if (uid > 255) uid = 255;
    s_u_n = uid;
    const char* nm = argc > 1 ? JS_ToCString(c, argv[1]) : 0;
    char args[300];
    sprintf(args, "%d,%d,%s", (int)d, uid, nm ? nm : "");
    if (nm) JS_FreeCString(c, nm);
    gl_emit(rc, "uloc", cid, args);
    return JS_NewInt32(c, uid);
}

// getAttribLocation(pid, name) -> proxy attrib id (host resolves)
static JSValue nfu_getAttribLocation(JSContext* c, JSValueConst thisv, int argc,
                                     JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, -1);
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    int aid = s_a_n + 1; if (aid > 255) aid = 255;
    s_a_n = aid;
    const char* nm = argc > 1 ? JS_ToCString(c, argv[1]) : 0;
    char args[300];
    sprintf(args, "%d,%d,%s", (int)d, aid, nm ? nm : "");
    if (nm) JS_FreeCString(c, nm);
    gl_emit(rc, "aloc", cid, args);
    return JS_NewInt32(c, aid);
}

// uniformMatrix2fv/3fv/4fv(loc, transpose, value) - magic 0/1/2 selects N
static JSValue nfu_uniformMatrix(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    int n = magic + 2;   // 2, 3 or 4 -> matrix side length
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    char args[600];
    int ap = sprintf(args, "%d", (int)d);
    if (argc > 2) append_nums(c, argv[2], args, ap, (int)sizeof(args), n * n);
    static const char* const nm[3] = { "uM2", "uM3", "uM4" };
    gl_emit(rc, nm[magic], cid, args);
    return JS_UNDEFINED;
}

static JSValue nfu_createVertexArray(JSContext* c, JSValueConst thisv, int argc,
                                     JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, 0);
    int vid = s_va_n + 1; if (vid > 255) vid = 255;
    s_va_n = vid;
    char args[32];
    sprintf(args, "%d", vid);
    gl_emit(rc, "vao", cid, args);
    return JS_NewInt32(c, vid);
}

static JSValue nfu_createTexture(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_NewInt32(c, 0);
    int tid = s_tx_n + 1; if (tid > 255) tid = 255;
    s_tx_n = tid;
    char args[32];
    sprintf(args, "%d", tid);
    gl_emit(rc, "tx", cid, args);
    return JS_NewInt32(c, tid);
}

// status queries the page code asks for but the bridge does not need to answer
// over the wire: compilation always "succeeded" from the page's point of view.
static JSValue nfu_ret_true(JSContext* c, JSValueConst thisv, int argc,
                            JSValueConst* argv, int magic) {
    if (magic == 0) return JS_NewBool(c, 1);
    if (magic == 1) return JS_NewInt32(c, 0);
    if (magic == 2) return JS_NewString(c, "");
    return JS_NULL;
}

// getContext('webgl'|'webgl2') -> gl object (emits glctx to create the context)
static JSValue make_gl_obj(JSContext* c, const char* cid);   // defined below
static JSValue nfu_getContext(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    nfu_trace(rc, "getContext");
    char* cid = canvas_cid_of(c, thisv);
    if (!cid) return JS_NULL;
    gl_emit(rc, "ctx", cid, "");
    return make_gl_obj(c, cid);
}

// DOM stub helpers for getElementById wrappers (React compat)
static JSValue nfu_dom_noop(JSContext* c, JSValueConst thisv, int argc, JSValueConst* argv) {
    if (argc > 0) return argv[0]; return JS_UNDEFINED;
}
static JSValue nfu_dom_noop2(JSContext* c, JSValueConst thisv, int argc, JSValueConst* argv) {
    return JS_NULL;
}
static JSValue nfu_dom_getattr(JSContext* c, JSValueConst thisv, int argc, JSValueConst* argv) {
    return JS_NULL;
}
static JSValue nfu_dom_rect(JSContext* c, JSValueConst thisv, int argc, JSValueConst* argv) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "x", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "y", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "width", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "height", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "top", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "left", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "right", JS_NewFloat64(c, 0));
    JS_SetPropertyStr(c, o, "bottom", JS_NewFloat64(c, 0));
    return o;
}

// document.getElementById(id) -> canvas wrapper
static JSValue nfu_getElementById(JSContext* c, JSValueConst thisv, int argc,
                                  JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    nfu_trace(rc, "getElementById");
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
    JS_SetPropertyStr(c, obj, "nodeType", JS_NewInt32(c, 1));
    JS_SetPropertyStr(c, obj, "nodeName", JS_NewString(c, "CANVAS"));
    JS_SetPropertyStr(c, obj, "appendChild", JS_NewCFunction(c, nfu_dom_noop, "appendChild", 1));
    JS_SetPropertyStr(c, obj, "insertBefore", JS_NewCFunction(c, nfu_dom_noop, "insertBefore", 2));
    JS_SetPropertyStr(c, obj, "removeChild", JS_NewCFunction(c, nfu_dom_noop, "removeChild", 1));
    JS_SetPropertyStr(c, obj, "replaceChild", JS_NewCFunction(c, nfu_dom_noop, "replaceChild", 2));
    JS_SetPropertyStr(c, obj, "addEventListener", JS_NewCFunction(c, nfu_dom_noop, "addEventListener", 2));
    JS_SetPropertyStr(c, obj, "removeEventListener", JS_NewCFunction(c, nfu_dom_noop, "removeEventListener", 2));
    JS_SetPropertyStr(c, obj, "setAttribute", JS_NewCFunction(c, nfu_dom_noop, "setAttribute", 2));
    JS_SetPropertyStr(c, obj, "removeAttribute", JS_NewCFunction(c, nfu_dom_noop, "removeAttribute", 1));
    JS_SetPropertyStr(c, obj, "getAttribute", JS_NewCFunction(c, nfu_dom_getattr, "getAttribute", 1));
    JS_SetPropertyStr(c, obj, "style", JS_NewObject(c));
    JS_SetPropertyStr(c, obj, "children", JS_NewArray(c));
    JS_SetPropertyStr(c, obj, "childNodes", JS_NewArray(c));
    JS_SetPropertyStr(c, obj, "classList", JS_NewObject(c));
    JS_SetPropertyStr(c, obj, "innerHTML", JS_NewString(c, ""));
    JS_SetPropertyStr(c, obj, "textContent", JS_NewString(c, ""));
    JS_SetPropertyStr(c, obj, "firstChild", JS_UNDEFINED);
    JS_SetPropertyStr(c, obj, "lastChild", JS_UNDEFINED);
    JS_SetPropertyStr(c, obj, "parentNode", JS_UNDEFINED);
    JS_SetPropertyStr(c, obj, "ownerDocument", JS_UNDEFINED);
    JS_SetPropertyStr(c, obj, "getBoundingClientRect", JS_NewCFunction(c, nfu_dom_rect, "getBoundingClientRect", 0));
    JS_SetPropertyStr(c, obj, "querySelector", JS_NewCFunction(c, nfu_dom_noop2, "querySelector", 1));
    JS_SetPropertyStr(c, obj, "querySelectorAll", JS_NewCFunction(c, nfu_dom_noop2, "querySelectorAll", 1));
    if (idv) JS_FreeCString(c, idv);
    return obj;
}

// DOM shim bridge: JS serializes the node tree to an HTML string written
// into the html buffer (overwrites; the browser re-parses the final value).
static JSValue nfu_nativeSync(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    nfu_trace(rc, "nativeSync");
    fprintf(stderr, "js: sync-IN argc=%d\n", argc); fflush(stderr);
    if (rc && rc->html && rc->html_sz > 1) {
        rc->hi = 0;
        if (argc > 0 && JS_IsString(argv[0])) {
            size_t L = 0;
            const char* h = JS_ToCStringLen(c, &L, argv[0]);
            fprintf(stderr, "js: sync-LEN=%zu\n", L); fflush(stderr);
            if (h) {
                HANDLE df = CreateFileA("E:\\d3ld\\sync_html.txt", GENERIC_WRITE, FILE_SHARE_READ,
                                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (df != INVALID_HANDLE_VALUE) {
                    char hdr[96];
                    int hl = snprintf(hdr, sizeof(hdr), "LEN=%zu FIRST500:\n", L);
                    DWORD wb = 0;
                    WriteFile(df, hdr, (DWORD)hl, &wb, NULL);
                    size_t dump = L < 500 ? L : 500;
                    WriteFile(df, h, (DWORD)dump, &wb, NULL);
                    CloseHandle(df);
                }
                if (L >= (size_t)rc->html_sz - 1) L = (size_t)rc->html_sz - 1;
                memcpy(rc->html, h, L);
                rc->hi = (int)L;
                JS_FreeCString(c, h);
            }
        }
        rc->html[rc->hi] = 0;
    }
    fprintf(stderr, "js: sync-OUT\n"); fflush(stderr);
    return JS_UNDEFINED;
}

// make a GL object bound to a canvas id (shared by canvas wrapper + DOM node)
static JSValue make_gl_obj(JSContext* c, const char* cid) {
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
    JS_SetPropertyStr(c, g, "uniform1f", JS_NewCFunctionMagic(c, nfu_gl, "uniform1f", 2, JS_CFUNC_generic_magic, 11));
    JS_SetPropertyStr(c, g, "uniform2f", JS_NewCFunctionMagic(c, nfu_gl, "uniform2f", 3, JS_CFUNC_generic_magic, 12));
    JS_SetPropertyStr(c, g, "uniform3f", JS_NewCFunctionMagic(c, nfu_gl, "uniform3f", 4, JS_CFUNC_generic_magic, 13));
    JS_SetPropertyStr(c, g, "uniform4f", JS_NewCFunctionMagic(c, nfu_gl, "uniform4f", 5, JS_CFUNC_generic_magic, 14));
    JS_SetPropertyStr(c, g, "uniform1i", JS_NewCFunctionMagic(c, nfu_gl, "uniform1i", 2, JS_CFUNC_generic_magic, 15));
    JS_SetPropertyStr(c, g, "uniform2i", JS_NewCFunctionMagic(c, nfu_gl, "uniform2i", 3, JS_CFUNC_generic_magic, 16));
    JS_SetPropertyStr(c, g, "uniform3i", JS_NewCFunctionMagic(c, nfu_gl, "uniform3i", 4, JS_CFUNC_generic_magic, 17));
    JS_SetPropertyStr(c, g, "uniform4i", JS_NewCFunctionMagic(c, nfu_gl, "uniform4i", 5, JS_CFUNC_generic_magic, 18));
    JS_SetPropertyStr(c, g, "drawElements", JS_NewCFunctionMagic(c, nfu_gl, "drawElements", 4, JS_CFUNC_generic_magic, 19));
    JS_SetPropertyStr(c, g, "enable", JS_NewCFunctionMagic(c, nfu_gl, "enable", 1, JS_CFUNC_generic_magic, 20));
    JS_SetPropertyStr(c, g, "disable", JS_NewCFunctionMagic(c, nfu_gl, "disable", 1, JS_CFUNC_generic_magic, 21));
    JS_SetPropertyStr(c, g, "blendFunc", JS_NewCFunctionMagic(c, nfu_gl, "blendFunc", 2, JS_CFUNC_generic_magic, 22));
    JS_SetPropertyStr(c, g, "depthFunc", JS_NewCFunctionMagic(c, nfu_gl, "depthFunc", 1, JS_CFUNC_generic_magic, 23));
    JS_SetPropertyStr(c, g, "cullFace", JS_NewCFunctionMagic(c, nfu_gl, "cullFace", 1, JS_CFUNC_generic_magic, 24));
    JS_SetPropertyStr(c, g, "activeTexture", JS_NewCFunctionMagic(c, nfu_gl, "activeTexture", 1, JS_CFUNC_generic_magic, 25));
    JS_SetPropertyStr(c, g, "texParameteri", JS_NewCFunctionMagic(c, nfu_gl, "texParameteri", 3, JS_CFUNC_generic_magic, 26));
    JS_SetPropertyStr(c, g, "texImage2D", JS_NewCFunctionMagic(c, nfu_gl, "texImage2D", 9, JS_CFUNC_generic_magic, 27));
    JS_SetPropertyStr(c, g, "bindTexture", JS_NewCFunctionMagic(c, nfu_gl, "bindTexture", 2, JS_CFUNC_generic_magic, 28));
    JS_SetPropertyStr(c, g, "bindVertexArray", JS_NewCFunctionMagic(c, nfu_gl, "bindVertexArray", 1, JS_CFUNC_generic_magic, 29));
    JS_SetPropertyStr(c, g, "getUniformLocation", JS_NewCFunction(c, nfu_getUniformLocation, "getUniformLocation", 2));
    JS_SetPropertyStr(c, g, "getAttribLocation", JS_NewCFunction(c, nfu_getAttribLocation, "getAttribLocation", 2));
    JS_SetPropertyStr(c, g, "getShaderParameter", JS_NewCFunctionMagic(c, nfu_ret_true, "getShaderParameter", 2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(c, g, "getProgramParameter", JS_NewCFunctionMagic(c, nfu_ret_true, "getProgramParameter", 2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(c, g, "getError", JS_NewCFunctionMagic(c, nfu_ret_true, "getError", 0, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "getShaderInfoLog", JS_NewCFunctionMagic(c, nfu_ret_true, "getShaderInfoLog", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "getProgramInfoLog", JS_NewCFunctionMagic(c, nfu_ret_true, "getProgramInfoLog", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "createTexture", JS_NewCFunction(c, nfu_createTexture, "createTexture", 0));
    return g;
}

// DOM node getContext('webgl2') -> gl object keyed by the node's id
static JSValue nfu_getGL(JSContext* c, JSValueConst thisv, int argc,
                         JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    const char* idv = argc > 0 ? JS_ToCString(c, argv[0]) : 0;
    if (!idv || !idv[0]) { if (idv) JS_FreeCString(c, idv); return JS_NULL; }
    gl_emit(rc, "ctx", idv, "");
    JSValue g = make_gl_obj(c, idv);
    JS_FreeCString(c, idv);
    return g;
}

// one shared runtime+context: global state survives across <script> blocks,
// exactly like a real browser
static JSRuntime* s_rt;
static JSContext* s_ctx;

static void ensure_js() {
    if (s_rt) return;
    s_rt = JS_NewRuntime();
    if (!s_rt) return;
#ifdef NEFU_BARE
    // Bare kernel has no per-thread 1MB guard page: JS_NewRuntime2's default
    // stack_limit would wrap on our 32-bit-style stack and reject every call.
    JS_SetMaxStackSize(s_rt, 256 * 1024);
#else
    // Win32 host: bound the JS stack so runaway sync recursion (e.g. nested
    // scheduleCallback through a sync setTimeout polyfill) throws RangeError
    // instead of growing into an uninterruptible hang.
    JS_SetMaxStackSize(s_rt, 8 * 1024 * 1024);
#endif
    JS_SetMemoryLimit(s_rt, 2048 * 1024 * 1024);  // 2GB: avoid runaway GC stalls on big bundles
    JS_SetGCThreshold(s_rt, 1024 * 1024 * 1024);  // only auto-GC past 1GB (React render churn)
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) { JS_FreeRuntime(s_rt); s_rt = 0; return; }
    JSValue g = JS_GetGlobalObject(s_ctx);
    JS_SetPropertyStr(s_ctx, g, "__nfuSync",
        JS_NewCFunction(s_ctx, nfu_nativeSync, "__nfuSync", 1));
    JS_SetPropertyStr(s_ctx, g, "__getGL",
        JS_NewCFunction(s_ctx, nfu_getGL, "__getGL", 1));
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
    JS_SetPropertyStr(s_ctx, doc, "querySelector",
        JS_NewCFunction(s_ctx, nfu_getElementById, "querySelector", 1));
    JS_SetPropertyStr(s_ctx, doc, "querySelectorAll",
        JS_NewCFunction(s_ctx, nfu_getElementById, "querySelectorAll", 1));
    JS_SetPropertyStr(s_ctx, doc, "createElement",
        JS_NewCFunction(s_ctx, nfu_getElementById, "createElement", 1));
    JS_SetPropertyStr(s_ctx, doc, "createTextNode",
        JS_NewCFunction(s_ctx, nfu_getElementById, "createTextNode", 1));
    JS_SetPropertyStr(s_ctx, doc, "addEventListener",
        JS_NewCFunction(s_ctx, nfu_dom_noop, "addEventListener", 2));
    JS_SetPropertyStr(s_ctx, doc, "removeEventListener",
        JS_NewCFunction(s_ctx, nfu_dom_noop, "removeEventListener", 2));
    JSValue dbody = JS_NewObjectClass(s_ctx, s_canvas_cls);
    JS_SetPropertyStr(s_ctx, dbody, "nodeType", JS_NewInt32(s_ctx, 1));
    JS_SetPropertyStr(s_ctx, dbody, "appendChild", JS_NewCFunction(s_ctx, nfu_dom_noop, "appendChild", 1));
    JS_SetPropertyStr(s_ctx, dbody, "insertBefore", JS_NewCFunction(s_ctx, nfu_dom_noop, "insertBefore", 2));
    JS_SetPropertyStr(s_ctx, dbody, "addEventListener", JS_NewCFunction(s_ctx, nfu_dom_noop, "addEventListener", 2));
    JS_SetPropertyStr(s_ctx, dbody, "setAttribute", JS_NewCFunction(s_ctx, nfu_dom_noop, "setAttribute", 2));
    JS_SetPropertyStr(s_ctx, dbody, "style", JS_NewObject(s_ctx));
    JS_SetPropertyStr(s_ctx, dbody, "children", JS_NewArray(s_ctx));
    JS_SetPropertyStr(s_ctx, dbody, "classList", JS_NewObject(s_ctx));
    JS_SetPropertyStr(s_ctx, dbody, "getBoundingClientRect", JS_NewCFunction(s_ctx, nfu_dom_rect, "getBoundingClientRect", 0));
    JS_SetPropertyStr(s_ctx, doc, "body", dbody);
    JS_SetPropertyStr(s_ctx, doc, "documentElement", JS_DupValue(s_ctx, dbody));
    JS_SetPropertyStr(s_ctx, doc, "head", JS_DupValue(s_ctx, dbody));
    JS_SetPropertyStr(s_ctx, doc, "createEvent", JS_NewCFunction(s_ctx, nfu_dom_noop2, "createEvent", 1));
    JS_SetPropertyStr(s_ctx, doc, "title", JS_NewString(s_ctx, s_env_title ? s_env_title : ""));
    JS_SetPropertyStr(s_ctx, doc, "readyState", JS_NewString(s_ctx, "complete"));
    JS_SetPropertyStr(s_ctx, doc, "URL", JS_NewString(s_ctx, s_env_url ? s_env_url : "about:blank"));
    JS_SetPropertyStr(s_ctx, g, "document", doc);

    // window === globalThis (browser semantics): window.print, window.alert,
    // window.document etc. resolve through the global object.
    // JS_SetPropertyStr consumes the value's reference, so each property needs
    // its own JS_DupValue of g (passing g twice would double-free the global).
    JSValue wnd = g;
    JS_SetPropertyStr(s_ctx, wnd, "window", JS_DupValue(s_ctx, g));
    JS_SetPropertyStr(s_ctx, wnd, "self", JS_DupValue(s_ctx, g));
    JS_SetPropertyStr(s_ctx, wnd, "innerWidth", JS_NewInt32(s_ctx, s_env_w > 0 ? s_env_w : 1024));
    JS_SetPropertyStr(s_ctx, wnd, "innerHeight", JS_NewInt32(s_ctx, s_env_h > 0 ? s_env_h : 768));
    JS_SetPropertyStr(s_ctx, wnd, "devicePixelRatio", JS_NewInt32(s_ctx, 1));
    JSValue loc = JS_NewObject(s_ctx);
    const char* envurl = s_env_url ? s_env_url : "about:blank";
    char loc_origin[512] = "http://localhost", loc_host[128] = "localhost", loc_hostname[128] = "localhost", loc_search[256] = "", loc_proto[16] = "http:";
    if (strncmp(envurl, "https://", 8) == 0) { strcpy(loc_proto, "https:"); }
    const char* p_h = strstr(envurl, "://");
    if (p_h) {
        const char* p_slash = strchr(p_h + 3, '/');
        const char* p_q = strchr(p_h + 3, '?');
        const char* hostend = p_slash ? p_slash : (p_q ? p_q : p_h + 3 + strlen(p_h + 3));
        size_t hn = (size_t)(hostend - (p_h + 3));
        if (hn > 0 && hn < 128) { memcpy(loc_host, p_h + 3, hn); loc_host[hn] = 0; }
        const char* p_colon = strchr(loc_host, ':');
        if (p_colon) { size_t h2 = (size_t)(p_colon - loc_host); if (h2 > 0 && h2 < 128) { memcpy(loc_hostname, loc_host, h2); loc_hostname[h2] = 0; } }
        else strcpy(loc_hostname, loc_host);
        if (p_q) {
            const char* qend = p_slash ? p_slash : p_q + strlen(p_q);
            size_t qn = (size_t)(qend - p_q);
            if (qn > 0 && qn < 256) { memcpy(loc_search, p_q, qn); loc_search[qn] = 0; }
        }
        snprintf(loc_origin, sizeof(loc_origin), "%s//%s", loc_proto, loc_host);
    }
    JS_SetPropertyStr(s_ctx, loc, "href", JS_NewString(s_ctx, envurl));
    JS_SetPropertyStr(s_ctx, loc, "protocol", JS_NewString(s_ctx, loc_proto));
    JS_SetPropertyStr(s_ctx, loc, "host", JS_NewString(s_ctx, loc_host));
    JS_SetPropertyStr(s_ctx, loc, "hostname", JS_NewString(s_ctx, loc_hostname));
    JS_SetPropertyStr(s_ctx, loc, "port", JS_NewString(s_ctx, ""));
    JS_SetPropertyStr(s_ctx, loc, "pathname", JS_NewString(s_ctx, "/"));
    JS_SetPropertyStr(s_ctx, loc, "search", JS_NewString(s_ctx, loc_search));
    JS_SetPropertyStr(s_ctx, loc, "hash", JS_NewString(s_ctx, ""));
    JS_SetPropertyStr(s_ctx, loc, "origin", JS_NewString(s_ctx, loc_origin));
    JS_SetPropertyStr(s_ctx, loc, "assign", JS_NewCFunction(s_ctx, nfu_dom_noop, "assign", 1));
    JS_SetPropertyStr(s_ctx, loc, "replace", JS_NewCFunction(s_ctx, nfu_dom_noop, "replace", 1));
    JS_SetPropertyStr(s_ctx, loc, "reload", JS_NewCFunction(s_ctx, nfu_dom_noop, "reload", 0));
    JS_SetPropertyStr(s_ctx, wnd, "location", loc);
    JSValue nav = JS_NewObject(s_ctx);
    JS_SetPropertyStr(s_ctx, nav, "userAgent", JS_NewString(s_ctx, "nefuOS/0.2 (host)"));
    JS_SetPropertyStr(s_ctx, nav, "appName", JS_NewString(s_ctx, "Netscape"));
    JS_SetPropertyStr(s_ctx, nav, "appVersion", JS_NewString(s_ctx, "nefuOS 0.2"));
    JS_SetPropertyStr(s_ctx, nav, "vendor", JS_NewString(s_ctx, ""));
    JS_SetPropertyStr(s_ctx, nav, "platform", JS_NewString(s_ctx, "Win32"));
    JS_SetPropertyStr(s_ctx, nav, "language", JS_NewString(s_ctx, "zh-CN"));
    JS_SetPropertyStr(s_ctx, nav, "languages", JS_NewString(s_ctx, "zh-CN,en-US"));
    JS_SetPropertyStr(s_ctx, nav, "onLine", JS_NewBool(s_ctx, 1));
    JS_SetPropertyStr(s_ctx, nav, "hardwareConcurrency", JS_NewInt32(s_ctx, 4));
    JS_SetPropertyStr(s_ctx, nav, "maxTouchPoints", JS_NewInt32(s_ctx, 0));
    JS_SetPropertyStr(s_ctx, g, "navigator", nav);
    // SetProperty consumed nav/loc: do NOT FreeValue them again (double free).

    // keep the doc JSValue alive: freeing it here would drop the reference
    // below the one held by the global object and break document.write
    JS_FreeValue(s_ctx, g);
    if (s_env_dom && s_env_dom[0])
        JS_SetPropertyStr(s_ctx, g, "__nfuSeed", JS_NewString(s_ctx, s_env_dom));
    // React/Vue-capable DOM shim: replaces document with a full node model
    JSValue shr = JS_Eval(s_ctx, JSDOM_SHIM_SRC, (size_t)strlen(JSDOM_SHIM_SRC),
                          "<dom-shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(shr)) {
        JSValue ex = JS_GetException(s_ctx);
        const char* m = JS_ToCString(s_ctx, ex);
        if (m) { fprintf(stderr, "jsdom shim error: %s\n", m); JS_FreeCString(s_ctx, m); }
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, shr);
    // minimal console so bundled apps (bloxd etc.) don't die on console.log;
    // log/warn/error route through print so the C-side rate-limited probe can
    // trace where a bundle stalls (empty functions would hide it entirely).
    const char* consrc =
        "if (typeof console === 'undefined' || !console.log) {"
        "  console = { log:function(){ print.apply(null, arguments); }, warn:function(){ print.apply(null, arguments); },"
        "             error:function(){ print.apply(null, arguments); }, info:function(){ print.apply(null, arguments); },"
        "             debug:function(){}, trace:function(){},"
        "             assert:function(){}, count:function(){}, table:function(){} };"
        "}";
    JSValue cok = JS_Eval(s_ctx, consrc, (size_t)strlen(consrc), "<console>",
                          JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(s_ctx, cok);
    // base64 helpers many bundles assume (atob/btoa)
    const char* b64src = R"js(
if (typeof atob === 'undefined') { atob = function(s) {
  var b='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/', out='', i=0, e1,e2,e3,c1,c2,c3,c4;
  s = String(s).replace(/[^A-Za-z0-9+/=]/g, '');
  s = s.replace(/=+$/, '');
  if ((s.length % 4) === 1) return out;
  while (i < s.length) {
    c1=b.indexOf(s[i++]); c2=b.indexOf(s[i++]); c3=b.indexOf(s[i++]); c4=b.indexOf(s[i++]);
    e1=(c1<<2)|(c2>>4);
    out += String.fromCharCode(e1);
    if (c3 >= 0) { e2=((c2&15)<<4)|(c3>>2); out += String.fromCharCode(e2); }
    if (c4 >= 0) { e3=((c3&3)<<6)|c4; out += String.fromCharCode(e3); }
  }
  return out; }; }
if (typeof btoa === 'undefined') { btoa = function(s) {
  var b='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/', out='', i=0;
  while (i < s.length) {
    var c1=s.charCodeAt(i++)&255, c2=s.charCodeAt(i++)&255, c3=s.charCodeAt(i++)&255;
    out += b.charAt(c1>>2) + b.charAt(((c1&3)<<4)|(c2>>4));
    if (isNaN(c2)) out += '=='; else out += b.charAt(((c2&15)<<2)|(c3>>6));
    if (isNaN(c3)) out += '='; else out += b.charAt(c3&63);
  }
  return out; }; }
)js";
    JSValue b64v = JS_Eval(s_ctx, b64src, (size_t)strlen(b64src), "<b64>",
                           JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(s_ctx, b64v);
    // common browser globals bundles assume; keep them cheap and non-fatal
    const char* browser_src = R"js(
if (typeof performance === 'undefined') performance = { now: function(){ return Date.now(); } };
if (typeof localStorage === 'undefined') localStorage = { getItem:function(){return null;}, setItem:function(){}, removeItem:function(){}, clear:function(){}, key:function(){return null;}, get length(){return 0;} };
if (typeof sessionStorage === 'undefined') sessionStorage = localStorage;
if (typeof screen === 'undefined') screen = { width:1920, height:1080, availWidth:1920, availHeight:1080, colorDepth:24 };
if (typeof window !== 'undefined' && typeof window.matchMedia === 'undefined') window.matchMedia = function(q){ return { matches:false, media:String(q), addListener:function(){}, removeListener:function(){}, addEventListener:function(){}, removeEventListener:function(){} }; };
if (typeof matchMedia === 'undefined' && typeof window !== 'undefined' && window.matchMedia) matchMedia = window.matchMedia;
if (typeof TextEncoder === 'undefined') { TextEncoder = function(){}; TextEncoder.prototype.encode = function(s){ s = String(s == null ? '' : s); var out = []; for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (c < 128) out.push(c); else if (c < 2048) { out.push(192|(c>>6), 128|(c&63)); } else { out.push(224|(c>>12), 128|((c>>6)&63), 128|(c&63)); } } return new Uint8Array(out); }; }
if (typeof TextDecoder === 'undefined') { TextDecoder = function(enc){ this.encoding = enc || 'utf-8'; }; TextDecoder.prototype.decode = function(buf){ if (buf == null) return ''; var u8 = (buf instanceof Uint8Array) ? buf : new Uint8Array(buf); var n = u8.length; if (n > 1048576) { print('[TDC-LIMIT] len=' + n); n = 1048576; } var parts = new Array(n); for (var i = 0; i < n; i++) parts[i] = String.fromCharCode(u8[i]); var out = parts.join(''); try { out = decodeURIComponent(escape(out)); } catch(e){} return out; }; }
if (typeof crypto === 'undefined') { crypto = { getRandomValues: function(arr){ for (var i=0;i<arr.length;i++) arr[i] = Math.floor(Math.random()*256); return arr; }, randomUUID: function(){ return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c){ var r = Math.random()*16|0, v = c=='x'?r:(r&0x3|0x8); return v.toString(16); }); }, subtle: {} }; }
if (typeof window !== 'undefined' && typeof window.crypto === 'undefined') window.crypto = crypto;
if (typeof document !== 'undefined' && document) { try { document.defaultView = (typeof window !== 'undefined') ? window : null; } catch(e){} }
if (!Object.hasOwn) Object.hasOwn = function(o, k){ return Object.prototype.hasOwnProperty.call(o, k); };
if (!Array.prototype.at) Array.prototype.at = function(i){ var n = this.length; i = i < 0 ? n + i : i; return (i < 0 || i >= n) ? undefined : this[i]; };
if (!String.prototype.replaceAll) String.prototype.replaceAll = function(a, b){ return this.split(a).join(b); };
if (typeof structuredClone === 'undefined') structuredClone = function(x){ try { return JSON.parse(JSON.stringify(x)); } catch(e){ return x; } };
if (typeof queueMicrotask === 'undefined') queueMicrotask = function(f){ try { Promise.resolve().then(f); } catch(e){} };
if (typeof globalThis === 'undefined') globalThis = (typeof window !== 'undefined') ? window : this;
if (typeof window !== 'undefined' && window) { try {
  ['HTMLIFrameElement','HTMLElement','HTMLDivElement','HTMLCanvasElement','HTMLImageElement','HTMLInputElement','HTMLButtonElement','HTMLTextAreaElement','HTMLSelectElement','HTMLFormElement','HTMLAnchorElement','HTMLLinkElement','HTMLScriptElement','HTMLStyleElement','HTMLVideoElement','HTMLAudioElement','HTMLBodyElement','HTMLHeadElement','SVGElement','SVGSVGElement','Element','Node','Document','Window','Event','MouseEvent','KeyboardEvent','PointerEvent','TouchEvent','CustomEvent','MutationObserver'].forEach(function(n){ if (typeof window[n] === 'undefined') { try { var T = function(){}; window[n] = T; } catch(e){} } });
} catch(e){} }
if (typeof history === 'undefined') history = { length:0, state:null, pushState:function(){}, replaceState:function(){}, back:function(){}, forward:function(){}, go:function(){} };
if (typeof window !== 'undefined' && typeof window.history === 'undefined') window.history = history;
if (typeof requestAnimationFrame === 'undefined') requestAnimationFrame = function(f){ if (typeof f === 'function') { try { f(Date.now()); } catch(e){} } return 1; };
if (typeof cancelAnimationFrame === 'undefined') cancelAnimationFrame = function(){};
if (typeof navigator !== 'undefined' && navigator) { try {
  if (!Array.isArray(navigator.languages)) navigator.languages = ['zh-CN', 'en-US'];
  if (typeof navigator.language !== 'string') navigator.language = 'zh-CN';
} catch(e){} }
if (typeof JSON !== 'undefined' && JSON.parse) { try {
  var __nfujp = JSON.parse;
  JSON.parse = function(s){
    if (s === null) return null;   // JSON.parse(null) === null (real semantics)
    if (typeof s !== 'string') {
      // Bypass: ToString on exotic objects (opaque stub / arraybuffer-like)
      // can stall inside quickjs's C ToString; real browsers stringify then
      // fail fast. Returning {} keeps the game code moving without hanging.
      print('[JPS-NSTR] ty=' + (typeof s));
      return {};
    }
    print('[JPS] len=' + s.length + ' head=' + s.slice(0, 80));
    var r = __nfujp(s);
    print('[JPE] len=' + s.length);
    return r;
  };
} catch(e){} }
if (typeof JSON !== 'undefined' && JSON.stringify) { try {
  var __nfujs = JSON.stringify;
  JSON.stringify = function(v, r, sp){ var out = __nfujs(v, r, sp); if (typeof out === 'string' && out.length > 512) { print('[JS] len=' + out.length); } return out; };
} catch(e){} }
if (typeof RegExp !== 'undefined' && RegExp.prototype) { try {
  var __reEx = RegExp.prototype.exec, __reTe = RegExp.prototype.test;
  var __reLt = 0, __reN = 0, __reDLt = 0;
  var __reDone = function(kind) {
    var now = (typeof Date !== 'undefined' && Date.now) ? Date.now() : 0;
    if (now - __reDLt >= 500) { __reDLt = now; print('[RED] ' + kind); }
  };
  var __rePr = function(kind, s, re) {
    __reN++;
    var now = (typeof Date !== 'undefined' && Date.now) ? Date.now() : 0;
    if (now - __reLt >= 500) { __reLt = now;
      var sl = (typeof s === 'string') ? s.length : -1;
      var src = (re && re.source) ? String(re.source).slice(0, 40) : '?';
      print('[RE] ' + kind + '#' + __reN + ' sl=' + sl + ' pat=' + src);
    }
  };
  RegExp.prototype.exec = function(s){ __rePr('ex', s, this); var r = __reEx.call(this, s); __reDone('ex'); return r; };
  RegExp.prototype.test = function(s){ __rePr('te', s, this); var r = __reTe.call(this, s); __reDone('te'); return r; };
  ['exec','test'].forEach(function(m){
    var f = RegExp.prototype[m];
    if (typeof f === 'function') RegExp.prototype[m] = function(s){
      if (typeof s === 'string' && s.length > 50000) __rePr('rge' + m, s, this);
      return f.call(this, s);
    };
  });
  var __stM = String.prototype.match, __stR = String.prototype.replace;
  String.prototype.match = function(re){ __rePr('ma', this, re instanceof RegExp ? re : null); return __stM.apply(this, arguments); };
  String.prototype.replace = function(re, rp){
    if (re instanceof RegExp) __rePr('rp', this, re);
    else if (typeof re === 'string' && this.length > 200) __rePr('rps', this, null);
    // Fast path: quickjs's C regexp replace can stall on huge inputs with a
    // simple literal pattern (e.g. /&/g over multi-MB strings). String
    // split/join is linear C code and returns promptly.
    if (re instanceof RegExp && re.global && typeof rp === 'string') {
      var src2 = re.source;
      if (src2 && src2.length <= 4 && src2.indexOf('\\') < 0) {
        try {
          var pr = this.split(re);   // split on the same pattern
          if (pr && pr.length > 1) {
            var joined = '';
            for (var i = 0; i < pr.length; i++) { if (i > 0) joined += rp; joined += pr[i]; }
            return joined;
          }
        } catch(e){}
      }
    }
    return __stR.apply(this, arguments);
  };
  ['split','join','slice','substring','indexOf','lastIndexOf','substr','trim','toLowerCase','toUpperCase','concat','charCodeAt','localeCompare'].forEach(function(m){
    var f = String.prototype[m];
    if (typeof f === 'function') String.prototype[m] = function(){
      if (typeof this === 'string' && this.length > 5000) __rePr('st' + m, this, null);
      return f.apply(this, arguments);
    };
  });
  if (typeof Array !== 'undefined' && Array.prototype) { try {
    ['push','pop','shift','unshift','splice','sort','map','filter','join','concat','slice','indexOf','includes','reverse'].forEach(function(m){
      var f = Array.prototype[m];
      if (typeof f === 'function') Array.prototype[m] = function(){
        if (this && typeof this.length === 'number' && this.length > 20000) __rePr('ar' + m, {length: this.length}, null);
        return f.apply(this, arguments);
      };
    });
  } catch(e){} }
  if (typeof Object !== 'undefined') { try {
    var __ok = Object.keys, __ov = Object.values;
    if (__ok) Object.keys = function(o){ if (o && typeof o === 'object') __rePr('keys', {length: (__ok(o)).length}, null); return __ok(o); };
    if (__ov) Object.values = function(o){ if (o && typeof o === 'object') __rePr('vals', {length: (__ov(o)).length}, null); return __ov(o); };
  } catch(e){} }
} catch(e){} }
if (typeof Function !== 'undefined' && Function.prototype && !window.__fnHook) { try {
  window.__fnHook = 1;
  var __Fn = Function;
  Function = function(){
    var code = arguments.length ? arguments[arguments.length - 1] : '';
    if (typeof code === 'string' && code.length > 300) { print('[FNC] len=' + code.length + ' head=' + code.slice(0, 50)); }
    return __Fn.apply(this, arguments);
  };
  Function.prototype = __Fn.prototype;
  Function.prototype.constructor = Function;
} catch(e){} }
if (typeof RegExp !== 'undefined' && RegExp.prototype && !window.__rgHook) { try {
  window.__rgHook = 1;
  var __Rg = RegExp;
  RegExp = function(p, f){
    if (typeof p === 'string' && p.length > 800) print('[RGX] len=' + p.length + ' head=' + p.slice(0, 80));
    return new __Rg(p, f);
  };
  RegExp.prototype = __Rg.prototype;
  RegExp.prototype.constructor = RegExp;
} catch(e){} }
if (typeof JSON !== 'undefined' && JSON.stringify && !window.__jsHook) { try {
  window.__jsHook = 1;
  var __jsf = JSON.stringify;
  JSON.stringify = function(o, r, sp){
    var res = __jsf.apply(this, arguments);
    if (res && res.length > 200000) print('[JSF] len=' + res.length + ' head=' + res.slice(0, 80));
    return res;
  };
} catch(e){} }
if (typeof TextDecoder !== 'undefined' && TextDecoder.prototype && TextDecoder.prototype.decode && !window.__tdHook) { try {
  window.__tdHook = 1;
  var __td = TextDecoder.prototype.decode;
  TextDecoder.prototype.decode = function(buf, o){
    var by = (buf && buf.length !== undefined) ? buf.length : -1;
    if (by > 1048576) { print('[TDC-LIMIT] len=' + by); if (buf && buf.slice) buf = buf.slice(0, 1048576); }
    return __td.call(this, buf, o);
  };
} catch(e){} }
if (typeof TextEncoder !== 'undefined' && TextEncoder.prototype && TextEncoder.prototype.encode && !window.__teHook) { try {
  window.__teHook = 1;
  var __te = TextEncoder.prototype.encode;
  TextEncoder.prototype.encode = function(s){
    if (typeof s === 'string' && s.length > 200000) print('[TEN] len=' + s.length);
    return __te.call(this, s);
  };
} catch(e){} }
['Uint8Array','Uint16Array','Uint32Array','Int8Array','Int16Array','Int32Array','Float32Array','Float64Array','ArrayBuffer'].forEach(function(nm){
  var C = (typeof window !== 'undefined' && window[nm]) ? window[nm] : (typeof globalThis !== 'undefined' ? globalThis[nm] : undefined);
  if (C && !(window.__taHook_ + nm)) {
    window.__taHook_ + nm;
    try {
      var F = function(l){
        var len = (typeof l === 'number') ? l : -1;
        if (len > 10000000) print('[TA] ' + nm + ' len=' + len);
        return new C(l);
      };
      F.prototype = C.prototype;
      if (C.from) F.from = C.from;
      if (C.of) F.of = C.of;
      if (window[nm]) window[nm] = F;
      if (typeof globalThis !== 'undefined') globalThis[nm] = F;
    } catch(e){}
  }
});
setTimeout = function(f, t){ if (typeof f === 'function') { try { Promise.resolve().then(f); } catch(e){} } return 0; };
clearTimeout = function(){};
setInterval = function(f, t){ return 0; };
clearInterval = function(){};
if (Array && Array.from) { try { var __nfufrom = Array.from; Array.from = function(x, fn, th){ try { return __nfufrom(x, fn, th); } catch(e) { if (x == null) return []; throw e; } }; } catch(e){} }
if (typeof fetch === 'undefined') fetch = function(){ return new Promise(function(res, rej){ rej(new Error('fetch not supported')); }); };
if (typeof WebSocket === 'undefined') WebSocket = function(url, p){ this.readyState = 3; this.CONNECTING=0; this.OPEN=1; this.CLOSING=2; this.CLOSED=3; };
if (typeof XMLHttpRequest === 'undefined') XMLHttpRequest = function(){ this.readyState=0; this.status=0; this.responseText=''; this.open=function(){}; this.send=function(){}; this.setRequestHeader=function(){}; };
if (typeof Image === 'undefined') Image = function(){ this.src=''; this.width=0; this.height=0; };
if (typeof URL === 'undefined') URL = function(u, b){
  this.href = String(u || ''); this.searchParams = { get:function(){ return null; }, has:function(){ return false; } };
  var m = /^(https?:)?\/\/([^/]+)(\/[^?#]*)?(\?[^#]*)?(#.*)?$/.exec(this.href);
  this.protocol = (m && m[1]) ? m[1] + ':' : 'https:';
  this.host = (m && m[2]) ? m[2] : '';
  this.hostname = this.host.split(':')[0];
  this.port = this.host.indexOf(':') >= 0 ? this.host.split(':')[1] : '';
  this.pathname = (m && m[3]) ? m[3] : '/';
  this.search = (m && m[4]) ? m[4] : '';
  this.hash = (m && m[5]) ? m[5] : '';
};
if (typeof URLSearchParams === 'undefined') URLSearchParams = function(s){ this.get=function(){return null;}; this.has=function(){return false;}; };
if (typeof Intl === 'undefined') Intl = {
  DateTimeFormat: function(){ return { format: function(){ return ''; }, resolvedOptions: function(){ return {}; } }; },
  NumberFormat: function(){ return { format: function(){ return ''; }, resolvedOptions: function(){ return {}; } }; },
  Collator: function(){ return { compare: function(a,b){ return a<b?-1:a>b?1:0; } }; }
};
if (typeof String !== 'undefined' && String.prototype && !String.prototype.includes) {
  try { String.prototype.includes = function(s){ return this.indexOf(s) >= 0; }; } catch(e){}
}
if (typeof Array !== 'undefined' && Array.prototype && !Array.prototype.includes) {
  try { Array.prototype.includes = function(s){ return this.indexOf(s) >= 0; }; } catch(e){}
}
if (typeof Error !== 'undefined' && Error.prototype) {
  try {
    Object.defineProperty(Error.prototype, 'stack',
      { value: '', writable: true, configurable: true });
  } catch(e){}
  try { Error.prototype.stack = ''; } catch(e){}
}
if (typeof document !== 'undefined') {
  if (typeof document.location === 'undefined' || !document.location) {
    try { document.location = window.location; } catch(e){}
  }
  if (typeof document.referrer === 'undefined') document.referrer = '';
  if (typeof document.readyState === 'undefined') document.readyState = 'complete';
  if (typeof document.addEventListener === 'undefined') document.addEventListener = function(){};
  if (typeof document.removeEventListener === 'undefined') document.removeEventListener = function(){};
}
if (typeof window !== 'undefined') {
  if (typeof window.devicePixelRatio === 'undefined') window.devicePixelRatio = 1;
  if (typeof window.innerWidth === 'undefined') window.innerWidth = 800;
  if (typeof window.innerHeight === 'undefined') window.innerHeight = 600;
  if (typeof window.addEventListener === 'undefined') window.addEventListener = function(t, f){ window['__on' + t] = f; };
  if (typeof window.removeEventListener === 'undefined') window.removeEventListener = function(){};
  if (typeof window.dispatchEvent === 'undefined') window.dispatchEvent = function(){ return true; };
  if (typeof window.postMessage === 'undefined') window.postMessage = function(){};
  if (typeof window.parent === 'undefined') window.parent = window;
  if (typeof window.top === 'undefined') window.top = window;
  if (typeof window.self === 'undefined') window.self = window;
  if (typeof window.frameElement === 'undefined') window.frameElement = null;
  ['chrome-extension','userscript','content.js','options.js','popup.js','.user.js','main.js','index.js','er.js','5','13','stack','captureStackTrace','stackTraceLimit','#IV']
    .forEach(function(n){ if (typeof window[n] === 'undefined') window[n] = function(){}; });
  if (typeof window.Error === 'undefined') window.Error = Error;
  window.Error = window.Error || Error;
  window["Error"] = window.Error;
  if (typeof window.stack === 'undefined') window.stack = '';
  try {
    if (Error && !Error.__nfuStackSafe) {
      Error.__nfuStackSafe = 1;
      var _nfusafe = function(msg){ try { var e = new Error(String(msg)); try { e.stack = ''; } catch(x){} e.message = String(msg); return e; } catch(x) { return { stack: '', message: String(msg) }; } };
      if (Error && Error.prototype) _nfusafe.prototype = Error.prototype;
      window.Error = _nfusafe;
      window["Error"] = _nfusafe;
    }
  } catch(e) {}
}
)js";
    JSValue brv = JS_Eval(s_ctx, browser_src, (size_t)strlen(browser_src), "<browser>",
                          JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(s_ctx, brv);
}

} // namespace

static volatile int s_js_lock = 0;   // serializes quickjs across threads
static inline void js_lock(void) {
    while (__sync_lock_test_and_set(&s_js_lock, 1)) {}
}
static inline void js_unlock(void) {
    __sync_lock_release(&s_js_lock);
}

extern "C" USHORT NTAPI RtlCaptureStackBackTrace(ULONG FramesToSkip,
                                                  ULONG FramesToCapture,
                                                  PVOID* BackTrace,
                                                  PULONG BackTraceHash);

static LONG WINAPI nfu_veh(PEXCEPTION_POINTERS ep) {
    if (ep && ep->ExceptionRecord &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        unsigned long long addr = (unsigned long long)ep->ExceptionRecord->ExceptionAddress;
        unsigned long long info0 = ep->ExceptionRecord->ExceptionInformation[0];
        unsigned long long info1 = ep->ExceptionRecord->ExceptionInformation[1];
        unsigned long long rip = ep->ContextRecord ? (unsigned long long)ep->ContextRecord->Rip : 0;
        unsigned long long rcx = ep->ContextRecord ? (unsigned long long)ep->ContextRecord->Rcx : 0;
        // capture the crash stack so the caller chain is visible
        void* frames[14] = {0};
        USHORT nf = RtlCaptureStackBackTrace(0, 14, frames, 0);
        unsigned long long base = (unsigned long long)GetModuleHandleA(NULL);
        char b[640];
        int bl = snprintf(b, sizeof(b),
            "CRASH av=%d addr=%llx info=%llx RIP=%llx RCX=%llx tid=%lu\nSTACK:",
            (int)(info0 == 0 ? 0 : info0 == 1 ? 1 : 2), addr, info1, rip, rcx,
            (unsigned long)GetCurrentThreadId());
        for (USHORT i = 0; i < nf && i < 14; i++) {
            unsigned long long f = (unsigned long long)frames[i];
            int extra = snprintf(b + bl, (size_t)sizeof(b) - (size_t)bl - 1, " %llx(off=%llx)",
                                 f, base ? f - base : 0);
            if (extra < 0 || bl + extra >= (int)sizeof(b) - 1) break;
            bl += extra;
        }
        HANDLE f = CreateFileA("E:\\d3ld\\crash97.txt", GENERIC_WRITE, FILE_SHARE_READ,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wb = 0;
            WriteFile(f, b, (DWORD)bl, &wb, NULL);
            CloseHandle(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifndef NEFU_BARE
static DWORD WINAPI nfu_watchdog(LPVOID p) {
    RunCtx* rc = (RunCtx*)p;
    uint32_t last_report = 0;
    for (;;) {
        if (rc->watch_done) break;
        uint32_t now = platform_tick_ms();
        if (now - last_report > 2000) {
            last_report = now;
            fprintf(stderr, "\njs: WD last_c=%s t=%u now=%u\n",
                    rc->last_c ? rc->last_c : "?", rc->last_c_t, now);
            fflush(stderr);
        }
        Sleep(250);
    }
    return 0;
}
#endif

int mini_js_run(const char* script, char* out, int out_sz,
                    char* html, int html_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    static int veh_once = 0;
    if (!veh_once) {
        veh_once = 1;
#ifndef NEFU_BARE
        AddVectoredExceptionHandler(1, nfu_veh);
#endif
    }
    out[0] = 0;
    js_lock();
    ensure_js();
    if (!s_ctx) return 1;
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    rc.html = html; rc.html_sz = html_sz; rc.hi = 0;
    if (html && html_sz > 0) html[0] = 0;
    rc.deadline = platform_tick_ms() + 90000;   // 90s script budget (bloxd bundles)
    rc.last_c = 0; rc.last_c_t = 0; rc.watch_done = 0;
#ifndef NEFU_BARE
    rc.jtid = GetCurrentThreadId();
    rc.exe_base = (unsigned long long)GetModuleHandleA(NULL);
#else
    rc.jtid = 0;
    rc.exe_base = 0;
#endif
    rc.rip_printed = 0; rc.rip_t = 0;
#ifndef NEFU_BARE
    HANDLE wd = CreateThread(NULL, 0, nfu_watchdog, &rc, 0, NULL);
#endif
    JS_SetInterruptHandler(JS_GetRuntime(s_ctx), js_interrupt_cb, &rc);
    JS_SetContextOpaque(s_ctx, &rc);
    // keep window.location / document.URL/title in sync with the tab
    if (s_env_url) {
        JSValue g2 = JS_GetGlobalObject(s_ctx);
        JSValue loc2 = JS_GetPropertyStr(s_ctx, g2, "location");
        if (JS_IsObject(loc2)) {
            JS_SetPropertyStr(s_ctx, loc2, "href", JS_NewString(s_ctx, s_env_url));
            const char* u = s_env_url;
            const char* h1 = strstr(u, "://");
            const char* host = h1 ? h1 + 3 : u;
            const char* h2 = strchr(host, '/');
            int hl = (h2 ? (int)(h2 - host) : (int)strlen(host));
            char hnb[128];
            if (hl > 0 && hl < (int)sizeof(hnb)) { memcpy(hnb, host, (size_t)hl); hnb[hl] = 0; }
            else { hnb[0] = 0; }
            JS_SetPropertyStr(s_ctx, loc2, "hostname", JS_NewString(s_ctx, hnb));
            JS_SetPropertyStr(s_ctx, loc2, "host", JS_NewString(s_ctx, hnb));
            char prot[16] = "https:";
            if (h1 && h1 >= u + 4 && memcmp(u, "http:", 5) == 0) memcpy(prot, "http:", 6);
            JS_SetPropertyStr(s_ctx, loc2, "protocol", JS_NewString(s_ctx, prot));
            JS_FreeValue(s_ctx, loc2);
        }
        JSValue doc2 = JS_GetPropertyStr(s_ctx, g2, "document");
        if (JS_IsObject(doc2)) {
            JS_SetPropertyStr(s_ctx, doc2, "URL", JS_NewString(s_ctx, s_env_url));
            if (s_env_title) JS_SetPropertyStr(s_ctx, doc2, "title", JS_NewString(s_ctx, s_env_title));
            JS_FreeValue(s_ctx, doc2);
        }
        JS_FreeValue(s_ctx, g2);
    }
    fprintf(stderr, "js: run %d bytes\n", (int)strlen(script));
    size_t script_len = strlen(script);
    if (script_len > 8) {
        unsigned char e0 = (unsigned char)script[script_len - 1];
        unsigned char e1 = (unsigned char)script[script_len];
        fprintf(stderr, "js: script tail byte=%02x nul=%02x\n", e0, e1);
        fflush(stderr);
    }
    const char* presrc = "try { var __si = 0, __to = 0; setInterval(function(){ __si = 7; }, 10); setTimeout(function(){ __to = 9; }, 10); 'SI:' + __si + '|TO:' + __to + '|ATOB:' + atob('RXJyb3I='); } catch(x) { 'WERRX:' + String(x && x.message); }";
    JSValue prv = JS_Eval(s_ctx, presrc, (size_t)strlen(presrc), "<pre>", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(prv)) {
        const char* pm = JS_ToCString(s_ctx, prv);
        if (pm) { fprintf(stderr, "js: pre: %s\n", pm); JS_FreeCString(s_ctx, pm); }
    } else { JSValue pe = JS_GetException(s_ctx); JS_FreeValue(s_ctx, pe); }
    JS_FreeValue(s_ctx, prv);
#ifndef NEFU_BARE
    fprintf(stderr, "js: eval-start t=%u tid=%lu\n", platform_tick_ms(),
            (unsigned long)GetCurrentThreadId());
#endif
    fputs("js: S eval-args\n", stderr); fflush(stderr);
    JSValue r = JS_Eval(s_ctx, script, script_len, "<script>",
                        JS_EVAL_TYPE_GLOBAL);
    fputs("js: S eval-return\n", stderr); fflush(stderr);
    fprintf(stderr, "js: run done\n");
    rc.watch_done = 1;
#ifndef NEFU_BARE
    if (wd) { WaitForSingleObject(wd, 2000); CloseHandle(wd); }
#endif
    int err = 0;
    if (JS_IsException(r)) {
        err = 1;
        fputs("js: S exc\n", stderr); fflush(stderr);
        JSValue ex = JS_GetException(s_ctx);
        const char* msg = JS_ToCString(s_ctx, ex);
        fputs("js: S exc-msg\n", stderr); fflush(stderr);
        if (msg) {
            ctx_emit(&rc, "E: ");
            ctx_emit(&rc, msg);
            JS_FreeCString(s_ctx, msg);
        }
        JSValue stk = JS_GetPropertyStr(s_ctx, ex, "stack");
        fputs("js: S exc-stk\n", stderr); fflush(stderr);
        if (JS_IsString(stk)) {
            const char* st = JS_ToCString(s_ctx, stk);
            if (st) { fprintf(stderr, "js: stack: %.500s\n", st); JS_FreeCString(s_ctx, st); }
        }
        JS_FreeValue(s_ctx, stk);
        JSValue em2 = JS_GetPropertyStr(s_ctx, ex, "message");
        fputs("js: S exc-em\n", stderr); fflush(stderr);
        if (JS_IsString(em2)) {
            const char* emv = JS_ToCString(s_ctx, em2);
            if (emv) { fprintf(stderr, "js: emsg: %.600s\n", emv); JS_FreeCString(s_ctx, emv); }
        }
        JS_FreeValue(s_ctx, em2);
        JSValue en2 = JS_GetPropertyStr(s_ctx, ex, "name");
        if (JS_IsString(en2)) {
            const char* env = JS_ToCString(s_ctx, en2);
            if (env) { fprintf(stderr, "js: ename: %.100s\n", env); JS_FreeCString(s_ctx, env); }
        }
        JS_FreeValue(s_ctx, en2);
        JS_FreeValue(s_ctx, ex);
        // diagnostic: window / window.Error state
        JSValue g3 = JS_GetGlobalObject(s_ctx);
        JSValue w3 = JS_GetPropertyStr(s_ctx, g3, "window");
        int winobj = JS_IsObject(w3);
        int winerr = 0;
        if (winobj) {
            JSValue e3 = JS_GetPropertyStr(s_ctx, w3, "Error");
            winerr = JS_IsFunction(s_ctx, e3);
            JSValue st3 = JS_GetPropertyStr(s_ctx, e3, "stackTraceLimit");
            int stl = !JS_IsUndefined(st3);
            JSValue m3 = JS_GetPropertyStr(s_ctx, w3, "main.js");
            int winmain = JS_IsFunction(s_ctx, m3);
            JSValue es = JS_ToString(s_ctx, e3);
            const char* esv = JS_ToCString(s_ctx, es);
            fprintf(stderr, "js: dbg win=%d winErrFn=%d stl=%d mainfn=%d errstr=%.30s\n", winobj, winerr, stl, winmain, esv ? esv : "?");
            if (esv) JS_FreeCString(s_ctx, esv);
            JS_FreeValue(s_ctx, es);
            JS_FreeValue(s_ctx, m3);
            JS_FreeValue(s_ctx, st3);
            JS_FreeValue(s_ctx, e3);
        }
        JS_FreeValue(s_ctx, w3);
        JS_FreeValue(s_ctx, g3);
        // try re-running Xj() in isolation
        const char* xjsrc = "try { var __xjr = String(Xj()); __xjr; } catch(x) { 'XJEXC:' + String(x && x.message); }";
        JSValue xv = JS_Eval(s_ctx, xjsrc, (size_t)strlen(xjsrc), "<xjtest>", JS_EVAL_TYPE_GLOBAL);
        if (!JS_IsException(xv)) {
            const char* xm = JS_ToCString(s_ctx, xv);
            if (xm) { fprintf(stderr, "js: XJ() = %s\n", xm); JS_FreeCString(s_ctx, xm); }
        } else {
            JSValue xe = JS_GetException(s_ctx);
            const char* xem = JS_ToCString(s_ctx, xe);
            if (xem) { fprintf(stderr, "js: XJ eval exc: %s\n", xem); JS_FreeCString(s_ctx, xem); }
            JS_FreeValue(s_ctx, xe);
        }
        JS_FreeValue(s_ctx, xv);
    }
    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    js_unlock();
    return err;
}

void mini_js_reset(void) {
    js_lock();
    if (s_ctx) { JS_FreeContext(s_ctx); s_ctx = 0; }
    if (s_rt) { JS_FreeRuntime(s_rt); s_rt = 0; }
    js_unlock();
}

} // namespace nefu
#endif
