/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS mini JS engine - minimal embeddable JavaScript-like interpreter.
// Numbers are integers (kernel has no FPU path), strings are fixed buffers.
// Parser structure inspired by "Let's Build a Simple Interpreter" (R. Spivak,
// MIT) and tiny-js (MIT); implementation is original, kernel-friendly (no
// dynamic allocation). Powers the browser <script> blocks and terminal `js`.
#include "minijs.h"
#include "jsdom_shim.h"
#include "wasm_js.h"
#include "../klib/klib.h"
#include "../../third_party/stb_image_wrap.h"

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

// navigator identity: identical to the UA the network layer sends
// (kChromeUa in backends/win32). Keeping the two in sync is what makes
// navigator.userAgent trustworthy to a bot check.
static const char* kNfuUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

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

// Preemption deadline for the "a click is waiting" case. A page script may
// legitimately run for tens of seconds, but a user gesture queued behind it
// must not starve: the UI thread sets this deadline when it parks a click and
// js_interrupt_cb (anonymous namespace below) cuts the script once it is
// reached. Kept at namespace scope so both sides see one variable.
static volatile uint32_t s_preempt_at = 0;

void mini_js_request_preempt(uint32_t after_ms) {
    s_preempt_at = platform_tick_ms() + after_ms;
}

void mini_js_clear_preempt() {
    s_preempt_at = 0;
}

// Binary GL upload staging lives in swgl.cpp. Vertex/index payloads are far
// bigger than the 8 KB per-frame command buffer, so the bytes travel out of
// band: the text line only carries a pool id.
unsigned nefu_glbin_put(const void* data, int len);
int nefu_glbin_take(unsigned id, const uint8_t** out);

namespace {

// navigator identity, duplicated inside this anonymous namespace so ensure_js
// sees it without relying on the outer static. Must stay byte-identical to
// kChromeUa in backends/win32: a page that compares navigator.userAgent with
// the request header treats a mismatch as a bot signature.
static const char* kNfuUaJs =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

// out-stream writer state shared with the JS callbacks below
struct RunCtx { char* out; int out_sz; int oi; uint32_t deadline;
             uint32_t hard_deadline;   // grace ceiling for one tick (0 = none)
             char* html; int html_sz; int hi;
             volatile const char* last_c; volatile uint32_t last_c_t;
             volatile uint32_t start_t;
             volatile int watch_done; volatile DWORD jtid;
             volatile unsigned long long exe_base; volatile int rip_printed; volatile uint32_t rip_t;
             };

// Dynamic import() chunks are parsed with JS_EVAL_FLAG_COMPILE_ONLY.  That
// parse runs inside a pending job whose RunCtx belongs to whichever tick
// happened to start it (mini_js_tick uses a 250 ms frame budget), and other
// ticks can replace the context opaque / restore an already expired deadline
// while the parse is in progress.  An interrupt in the middle of a token makes
// quickjs report a RANDOM SyntaxError ("invalid number literal",
// "expecting ';'", "unexpected end of string", ...).  The bytes are fine:
// the very same chunk compiles on the next retry.  Verified locally with
// verify_out/modcompile.exe against real bloxd.io chunks.
//
// Keep module parsing on its own independent budget so no other tick can cut
// it short.  A depth counter (not a bool) also covers nested loaders, and a
// hard cap still stops a genuinely runaway parse.
static volatile int      g_mod_parse_depth    = 0;
static volatile uint32_t g_mod_parse_deadline = 0;
static const uint32_t    kModParseBudgetMs    = 120000;

// trace which native (C) call the JS thread last entered, for the watchdog
static void nfu_trace(RunCtx* rc, const char* what) {
    if (!rc) return;
    rc->last_c = what;
    rc->last_c_t = platform_tick_ms();
}

// kill runaway scripts after the 1s budget: JS_SetInterruptHandler fires
// between bytecode instructions, so this stops infinite loops cleanly

// Memory runaway fuse. chat.deepseek.com's init recursion grew the heap from
// 15 MB to 1.1 GB in ~12 s and then crashed inside JS_DefineProperty on a
// half-initialized object (NULL shape) once allocations started failing near
// the 2 GB runtime cap. The fuse trips far below the cap: the interrupt
// handler fires between bytecode instructions, so returning 1 here aborts
// the page's JS cleanly and the browser keeps running (like a crashed tab in
// a real browser, not a kernel panic). Reset per page in mini_js_reset().
static volatile int s_mem_dead = 0;
// chat.deepseek.com's init recursion is genuinely unbounded (90 s trace:
// 16 MB -> 1.5 GB and still climbing) — the fuse at 1.4 GB just decides
// how much RAM it burns before the graceful-degradation dialog shows.
// Keep 1.4 GB on both backends: earlier trip = less memory + faster dialog.
static const long long kMemFuseBytes = ((long long)1400) * 1024 * 1024;

// (the preemption deadline and its setters live above, at namespace nefu scope:
// an anonymous namespace would give them internal linkage and browser.cpp
// could not link against them)

// The tick deadline is a fixed wall-clock budget measured from the start of the
// frame. A page that legitimately hands a large job to the engine in one tick
// (bloxd compiles and re-wraps a 334 KB wasm module and then runs its whole
// .then() chain as microtasks before yielding) hits that budget mid-work and
// gets cut off with "interrupted", which the page reports as "Bloxd couldn't
// start in this browser". Native entry points that begin such a job can push
// the deadline out; the microtask chain that follows runs in the same tick and
// therefore inherits the extension.
static RunCtx* s_live_rc = 0;
extern "C" void nfu_js_extend_deadline(uint32_t ms) {
    RunCtx* rc = s_live_rc;
    if (!rc) return;
    uint32_t t = platform_tick_ms() + ms;
    if ((int32_t)(t - rc->deadline) > 0) rc->deadline = t;
}
static int js_interrupt_cb(JSRuntime* rt, void* opaque) {
    RunCtx* rc = (RunCtx*)opaque;
    if (!rc) return 0;
    s_live_rc = rc;
    if (s_mem_dead) return 1;
#ifndef NEFU_BARE
    static uint32_t last_t = 0;
    static uint32_t last_mem_chk = 0;
    static uint32_t intr_n = 0, intr_log_t = 0;
    uint32_t now = platform_tick_ms();
    {
        // Proof of life for the interrupt channel itself.  A tick that runs for
        // minutes without ever reaching this callback means the handler is not
        // installed (or points at another thread's context) and no deadline can
        // ever fire - which looks exactly like a page stuck in native code.
        intr_n++;
        if (now - intr_log_t > 2000) {
            intr_log_t = now;
            fprintf(stderr, "\njs: INTR n=%u now=%u dl=%u depth=%d\n",
                    (unsigned)intr_n, now, rc->deadline, (int)g_mod_parse_depth);
            fflush(stderr);
        }
    }
    if (now - last_mem_chk > 2000) {   // fuse check every 2 s (compute only;
                                       // the print stays on the 10 s cadence)
        last_mem_chk = now;
        JSMemoryUsage mu;
        JS_ComputeMemoryUsage(rt, &mu);
        if ((long long)mu.memory_used_size > kMemFuseBytes) {
            s_mem_dead = 1;
            fprintf(stderr, "\njs: MEMORY FUSE tripped at %lld bytes - page JS aborted\n",
                    (long long)mu.memory_used_size);
            fflush(stderr);
            return 1;
        }
    }
    if (now - last_t > 10000) {   // memory+log every 10s (was 1s): the per-1s
                                  // JS_ComputeMemoryUsage + stdout fprintf in
                                  // this hot callback measurably slowed bloxd's
                                  // deobfuscation loops
        last_t = now;
        rc->last_c_t = now;   // progress marker for the watchdog
        JSMemoryUsage mu;
        JS_ComputeMemoryUsage(rt, &mu);
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "\njs: t=%u dl=%u mem=%lld\n", now, rc->deadline, (long long)mu.memory_used_size);
        if (n > 0) { fwrite(buf, 1, (size_t)n, stdout); fflush(stdout); }
    }
    // While a module is being parsed, ignore the tick's frame deadline (it may
    // be stale, expired, or belong to a finished tick) and use the dedicated
    // parse budget instead.  Otherwise quickjs aborts mid-token and reports a
    // bogus SyntaxError on perfectly valid downloaded bytes.
    uint32_t eff_deadline = g_mod_parse_depth > 0 ? g_mod_parse_deadline : rc->deadline;
    // A queued click cuts the current script: better a partially-run page
    // script than a page whose buttons never respond at all.
    uint32_t pre = s_preempt_at;
    if (pre && now >= pre) {
        s_preempt_at = 0;
        fprintf(stderr, "\njs: preempted at %u ms (input waiting)\n", now);
        fflush(stderr);
        return 1;
    }
    if (now > eff_deadline) {
        // Grace window.  A single tick can legitimately park the JS thread
        // inside native code (wasm3 module instantiation, Draco decoding,
        // byte-wise base64 of model blobs) for tens of seconds; quickjs only
        // checks this callback when it returns to bytecode, so the deadline
        // fires *after* the native work and cuts an async chain that was about
        // to finish (bloxd's room join then dies mid-handshake and the server
        // closes with a bare error frame).  While the tick is still inside its
        // hard ceiling, pay out another frame budget instead of interrupting.
        if (g_mod_parse_depth == 0 && rc->hard_deadline && (int32_t)(rc->hard_deadline - now) > 0) {
            static int grace_n = 0;
            rc->deadline = now + 6000;
            if (grace_n < 20) {
                grace_n++;
                fprintf(stderr, "\njs: [JS-GRACE] now=%u over=%d hard=%u\n",
                        now, (int)now - (int)eff_deadline, rc->hard_deadline);
                fflush(stderr);
            }
            return 0;
        }
        // Which budget cut the script, and by how much. Without this the page
        // only reports "InternalError: interrupted" with a stack inside
        // whichever job happened to be running, and the real cause is invisible.
        static int int_n = 0;
        if (int_n < 20) {
            int_n++;
            fprintf(stderr, "\njs: [JS-INT] now=%u dl=%u over=%d parse=%d\n",
                    now, eff_deadline, (int)now - (int)eff_deadline, (int)g_mod_parse_depth);
            fflush(stderr);
        }
        return 1;
    }
    return 0;
#else
    // The bare kernel has no host-side watchdog thread. A 3.36 MB obfuscated
    // webpack bundle can spend minutes in JS_Eval under QEMU TCG; without a
    // heartbeat the whole run looks like a hang (zero serial output). Emit
    // one progress line every 10 s so we can tell "slow but alive" from a
    // genuine spin, and see whether memory is still growing.
    static uint32_t last_t = 0;
    uint32_t now = platform_tick_ms();
    if (now - last_t >= 10000u) {
        last_t = now;
        JSMemoryUsage mu;
        JS_ComputeMemoryUsage(rt, &mu);
        if ((long long)mu.memory_used_size > kMemFuseBytes) {
            s_mem_dead = 1;   // same runaway fuse as host: a page that grows
                              // the heap past ~1.4 GB must be stopped before
                              // allocation failures corrupt quickjs and
                              // panic the kernel
            klogf("js: MEMORY FUSE tripped - page JS aborted\n");
        }
        klogf("js: t=%u dl=%u mem=%lld last=%s\n", now, rc->deadline,
              (long long)mu.memory_used_size,
              rc->last_c ? (const char*)rc->last_c : "-");
    }
    // While a module is being parsed, ignore the tick's frame deadline (it may
    // be stale, expired, or belong to a finished tick) and use the dedicated
    // parse budget instead.  Otherwise quickjs aborts mid-token and reports a
    // bogus SyntaxError on perfectly valid downloaded bytes.
    uint32_t eff_deadline = g_mod_parse_depth > 0 ? g_mod_parse_deadline : rc->deadline;
    return now > eff_deadline ? 1 : 0;
#endif
}

static void ctx_emit(RunCtx* rc, const char* s) {
    // Defensive: rc points at the caller's stack frame; a stale pointer (or a
    // torn buffer) must never fault here.
    if (!rc || !rc->out || rc->out_sz <= 1 || rc->oi < 0 || rc->oi >= rc->out_sz) return;
    while (*s && rc->oi < rc->out_sz - 1) rc->out[rc->oi++] = *s++;
    // A page can print far more than the buffer holds. Silently dropping the
    // tail made the missing diagnostics look like "the page printed
    // nothing", so leave a marker that says the output was cut off.
    if (*s && rc->oi >= rc->out_sz - 1) {
        static const char trunc[] = "\n[output truncated]";
        int room = (int)(rc->out_sz - 1 - rc->oi);
        int n = (int)sizeof(trunc) - 1;
        if (n > room) n = room;
        for (int i = 0; i < n; i++) rc->out[rc->oi++] = trunc[i];
    }
    rc->out[rc->oi] = 0;
}
static void ctx_emitc(RunCtx* rc, char c) {
    if (!rc || !rc->out || rc->out_sz <= 1 || rc->oi < 0 || rc->oi >= rc->out_sz) return;
    rc->out[rc->oi++] = c;
    rc->out[rc->oi] = 0;
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
        int urgent = 0;
        if (mark) {
            // diagnostic-tagged prints ([XXX] ... from the shim / test pages)
            // bypass the rate limit: they ARE the tracing channel
            if (mark[0] == '[' ||
                strstr(mark, "[MARK]") || strstr(mark, "libraries loaded") ||
                strstr(mark, "Turnstile") || strstr(mark, "Failed to"))
                urgent = 1;
        }
        if (now - last_log_t >= 500 || urgent) {
            char lb[512];
            int ln = 0;
            if (mark) { ln = snprintf(lb, sizeof(lb), "log#%u t=%u: %.260s", log_n, now, mark); }
            else ln = snprintf(lb, sizeof(lb), "log#%u t=%u: (noarg)", log_n, now);
            if (ln > 0) { fwrite(lb, 1, (size_t)ln, stderr); fputc('\n', stderr); fflush(stderr); }
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
static int s_u_n = 0, s_a_n = 0, s_tx_n = 0;   // uniform/attrib loc, texture ids

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

// Fixed 1600-byte container for the *short* commands (ids, enums, counts).
// Anything carrying real geometry goes through the pool below instead, because
// a single chunk mesh is orders of magnitude larger than the frame budget.
static void ctx_emitn(RunCtx* rc, const char* s, int n) {
    if (!rc || !rc->out) return;
    for (int i = 0; i < n && rc->oi < rc->out_sz - 1; i++) rc->out[rc->oi++] = s[i];
}

// Same line format as gl_emit, but the payload is sized at runtime.
static void gl_emit_big(RunCtx* rc, const char* sub, const char* cid,
                        const char* args) {
    static const char* kHex = "0123456789ABCDEF";
    int alen = (int)strlen(args);
    int cap = alen * 3 + 8;
    char* enc = (char*)malloc((size_t)cap);
    if (!enc) return;
    int n = 0;
    for (int i = 0; i < alen; i++) {
        unsigned char ch = (unsigned char)args[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == '_') {
            enc[n++] = (char)ch;
        } else {
            enc[n++] = '%'; enc[n++] = kHex[ch >> 4]; enc[n++] = kHex[ch & 15];
        }
    }
    ctx_emitc(rc, '\x01');
    ctx_emit(rc, "gl"); ctx_emit(rc, sub);
    ctx_emitc(rc, '|'); ctx_emit(rc, cid);
    ctx_emitc(rc, '|');
    ctx_emitn(rc, enc, n);
    ctx_emitc(rc, '\n');
    free(enc);
}

// Raw bytes behind an ArrayBuffer or any typed-array view (null when the value
// is not binary at all, which is the normal case for the legacy float form).
static void js_clear_exc(JSContext* c) {
    // JS_GetTypedArrayBuffer/JS_GetArrayBuffer both throw TypeErrors for the
    // wrong class; the exception has to be taken off the context or the next
    // eval inherits it and reports a bogus throw from unrelated code.
    JSValue ex = JS_GetException(c);
    JS_FreeValue(c, ex);
}

static const uint8_t* js_bin_of(JSContext* c, JSValueConst v, int* outLen) {
    *outLen = 0;
    if (!JS_IsObject(v)) return 0;
    size_t off = 0, len = 0, esz = 1;
    JSValue ab = JS_GetTypedArrayBuffer(c, v, &off, &len, &esz);
    if (JS_IsException(ab)) {
        js_clear_exc(c);
        // Plain ArrayBuffer (not a view): the whole object is the payload.
        size_t sl = 0;
        uint8_t* ap = JS_GetArrayBuffer(c, &sl, v);
        if (!ap) { js_clear_exc(c); return 0; }
        *outLen = (int)sl;
        return ap;
    }
    // A view only covers [off, off+len) of the parent buffer; uploading the
    // whole parent would shift every vertex by the view's offset.
    size_t blen = 0;
    uint8_t* base = JS_GetArrayBuffer(c, &blen, ab);
    JS_FreeValue(c, ab);
    if (!base) return 0;
    if (off + len > blen) len = (blen > off) ? (blen - off) : 0;
    *outLen = (int)len;
    return base + off;
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

// Page-requested navigation: location.href = ... , location.assign/replace,
// location.reload(), history.back()/forward() and form submission all end up
// here. The shim has no access to the browser's tab state, so the intent is
// encoded as a "\x01nav|<mode>|<urlenc>" control line on the very stream the GL
// bridge uses; browser.cpp parks it and applies it on the next frame (a load
// started from inside a script would re-enter the loader).
// mode: 0 = load the URL, 1 = address only (pushState), 2 = reload, 3 = back,
//       4 = forward
static JSValue nfu_go(JSContext* c, JSValueConst thisv, int argc,
                      JSValueConst* argv) {
    (void)thisv;
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    int mode = 0;
    const char* url = "";
    char* owned = 0;
    if (argc > 1 && JS_IsNumber(argv[1])) {
        int32_t m = 0;
        if (JS_ToInt32(c, &m, argv[1]) == 0) mode = (int)m;
    }
    // Only stringify real strings (same reason as nfu_out: a native
    // ToString on exotic object stubs can stall inside quickjs).
    if (argc > 0 && JS_IsString(argv[0])) {
        owned = (char*)JS_ToCString(c, argv[0]);
        if (owned) url = owned;
    }
    char enc[1600];
    urlenc(url, enc, sizeof(enc));
    if (owned) JS_FreeCString(c, owned);
    if (mode < 0) mode = 0;
    if (mode > 9) mode = 9;
    ctx_emitc(rc, '\x01');
    ctx_emit(rc, "nav|");
    ctx_emitc(rc, (char)('0' + mode));
    ctx_emitc(rc, '|');
    ctx_emit(rc, enc);
    ctx_emitc(rc, '\n');
    return JS_UNDEFINED;
}

// Form POST from the page: same parking contract as nfu_go, plus the body.
// Emits "\x01nav|5|<urlenc url>|<urlenc body>"; both halves are encoded, so
// '|' survives only as the separator the browser splits on.
static JSValue nfu_goPost(JSContext* c, JSValueConst thisv, int argc,
                          JSValueConst* argv) {
    (void)thisv;
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* owned_url = 0;
    char* owned_body = 0;
    const char* url = "";
    const char* body = "";
    if (argc > 0 && JS_IsString(argv[0])) {
        owned_url = (char*)JS_ToCString(c, argv[0]);
        if (owned_url) url = owned_url;
    }
    if (argc > 1 && JS_IsString(argv[1])) {
        owned_body = (char*)JS_ToCString(c, argv[1]);
        if (owned_body) body = owned_body;
    }
    char enc[1600];
    char benc[3100];
    // Percent-encoding triples the size, and this line shares the 32KB emit
    // buffer with everything else the frame prints. Cap the body at a field
    // boundary so a huge textarea cannot cut the command in half.
    size_t blen = strlen(body);
    char bcut[2501];
    if (blen > 2500) {
        blen = 2500;
        while (blen > 0 && body[blen] != '&') blen--;
        if (blen == 0) blen = 2500;
    }
    memcpy(bcut, body, blen);
    bcut[blen] = 0;
    urlenc(url, enc, sizeof(enc));
    urlenc(bcut, benc, sizeof(benc));
    if (owned_url) JS_FreeCString(c, owned_url);
    if (owned_body) JS_FreeCString(c, owned_body);
    ctx_emitc(rc, '\x01');
    ctx_emit(rc, "nav|5|");
    ctx_emit(rc, enc);
    ctx_emitc(rc, '|');
    ctx_emit(rc, benc);
    ctx_emitc(rc, '\n');
    return JS_UNDEFINED;
}

// Last canvas id that produced a GL context. Babylon (bloxd's renderer) wraps
// the context in a Proxy, so `this` inside createShader/createBuffer/... is the
// proxy object, which carries no opaque tag of ours. Returning NULL there made
// every create* call answer 0, and bloxd read that as "Something went wrong
// while creating a gl shader object" and aborted the whole effect pipeline -
// which is exactly the empty world we were staring at. One page, one context,
// so the last bound id is the right answer for any receiver we cannot tag.
static char s_gl_last_cid[64];
static char* gl_cid_of(JSContext* c, JSValueConst thisv) {
    (void)c;
    if (JS_IsObject(thisv)) {
        char* op = (char*)JS_GetOpaque(thisv, s_gl_cls);
        if (op && op[0]) {
            snprintf(s_gl_last_cid, sizeof(s_gl_last_cid), "%s", op);
            return op;
        }
    }
    return s_gl_last_cid[0] ? s_gl_last_cid : 0;
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
    "bva",
    // WebGL1 state setters the swgl command set already implements
    "dmask", "cmask", "scis", "pixi", "tsub"
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
    } else if (magic == 30) {   // depthMask(flag) -> swgl "dmask"
        double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
        ap = sprintf(args, "%d", d != 0);
    } else if (magic == 31) {   // colorMask(r,g,b,a) -> swgl "cmask"
        int v[4] = {1, 1, 1, 1};
        int n = argc > 4 ? 4 : argc;
        for (int i = 0; i < n; i++) { double d = 1; JS_ToFloat64(c, &d, argv[i]); v[i] = d != 0; }
        ap = sprintf(args, "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
    } else if (magic == 32) {   // scissor(x,y,w,h) -> swgl "scis"
        int v[4] = {0, 0, 0, 0};
        int n = argc > 4 ? 4 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d,%d,%d", v[0], v[1], v[2], v[3]);
    } else if (magic == 33) {   // pixelStorei(pname, param) -> swgl "pixi"
        int v[2] = {0, 0};
        int n = argc > 2 ? 2 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        ap = sprintf(args, "%d,%d", v[0], v[1]);
    } else if (magic == 34) {   // texSubImage2D(target,level,xo,yo,w,h,format,type[,pixels])
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        int n = argc > 8 ? 8 : argc;
        for (int i = 0; i < n; i++) { double d = 0; JS_ToFloat64(c, &d, argv[i]); v[i] = (int)d; }
        // pixel data like texImage2D: upload the bytes into the worker-side
        // pending slot so the UI thread can apply them when "tsub" runs.
        if (argc > 8 && !JS_IsUndefined(argv[8]) && JS_IsObject(argv[8])) {
            size_t off2 = 0, blen2 = 0, bpe2 = 1;
            JSValue ab2 = JS_GetTypedArrayBuffer(c, argv[8], &off2, &blen2, &bpe2);
            if (!JS_IsException(ab2)) {
                size_t absz2 = 0;
                uint8_t* base2 = JS_GetArrayBuffer(c, &absz2, ab2);
                if (base2 && off2 + blen2 <= absz2 && blen2 > 0) {
                    uint8_t* px = base2 + off2;
                    gl_tex_upload(cid, v[0], v[1], 0, v[4], v[5], v[6], v[7], px, (int)blen2);
                }
                JS_FreeValue(c, ab2);
            } else {
                JS_FreeValue(c, JS_GetException(c));
            }
        }
        ap = sprintf(args, "%d,%d,%d,%d,%d,%d,%d,%d", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    }
    if (magic == 10) return JS_UNDEFINED;   // enableVertexAttribArray: done by glattptr
    args[ap] = 0;
    gl_emit(rc, glcmd_names[magic], cid, args);
    return JS_UNDEFINED;
}

// createShader(type) -> sid; type remembered for shaderSource
static int s_sh_type[256];

// WebGL handles have to be *objects*. Babylon attaches bookkeeping to them
// (program.__SPECTOR_rebuildProgram, shader.onCompiled, custom props on
// buffers), and writing a property on a Number primitive throws a TypeError
// under strict mode - bloxd caught exactly that, logged "Error compiling
// effect", and left the world empty. The wrapper carries the numeric id plus a
// valueOf() so every existing JS_ToFloat64 conversion in this bridge still
// resolves to the same integer, unchanged.
static JSValue nfu_valof(JSContext* c, JSValueConst thisv, int argc,
                         JSValueConst* argv, int magic) {
    (void)thisv; (void)argc; (void)argv;
    return JS_NewInt32(c, magic);
}
// toString(): WebGL location handles are used as map keys by Babylon
// (this._boundUniforms[location]), so each location must stringify to its own
// distinct value instead of the generic "[object Object]".
static JSValue nfu_handle_str(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv, int magic) {
    (void)thisv; (void)argc; (void)argv;
    char b[32];
    snprintf(b, sizeof(b), "nfuLoc%d", magic);
    return JS_NewString(c, b);
}
static JSValue nfu_handle_obj(JSContext* c, int id, const char* kind) {
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "__nfuH", JS_NewInt32(c, id));
    JS_SetPropertyStr(c, o, "valueOf",
                      JS_NewCFunctionMagic(c, nfu_valof, "valueOf", 0,
                                           JS_CFUNC_generic_magic, id));
    JS_SetPropertyStr(c, o, "toString",
                      JS_NewCFunctionMagic(c, nfu_handle_str, "toString", 0,
                                           JS_CFUNC_generic_magic, id));
    if (kind) JS_SetPropertyStr(c, o, "__nfuKind", JS_NewString(c, kind));
    return o;
}

static JSValue nfu_createShader(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    char* cid = gl_cid_of(c, thisv);
    if (!cid) {
        static int s_sh_fail = 0;
        if (s_sh_fail < 8) { s_sh_fail++; fprintf(stderr, "\n[GSH] createShader no-context\n"); fflush(stderr); }
        return nfu_handle_obj(c, 0, "shader");
    }
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    int sid = s_sh_n + 1; if (sid > 255) sid = 255;
    s_sh_n = sid;
    s_sh_type[sid] = (int)d;
    return nfu_handle_obj(c, sid, "shader");
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
    // Shader sources run to several KB once babylon injects its defines; the
    // 1400-byte text line used to truncate them, so every program failed to
    // compile and the game rendered a black canvas. Hand the bytes over through
    // the out-of-band pool and let the command carry only the pool id.
    unsigned gid = sl > 1000 ? nefu_glbin_put(src, (int)sl) : 0;
    if (gid) {
        char args[96];
        sprintf(args, "%d,%d,%u,%d", sid, s_sh_type[sid], gid, (int)sl);
        gl_emit_big(rc, "shb", cid, args);
        JS_FreeCString(c, src);
        return JS_UNDEFINED;
    }
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
    if (!cid) return nfu_handle_obj(c, 0, "program");
    int pid = s_pr_n + 1; if (pid > 255) pid = 255;
    s_pr_n = pid;
    char args[64];
    sprintf(args, "%d", pid);
    gl_emit(rc, "prog", cid, args);
    return nfu_handle_obj(c, pid, "program");
}

static JSValue nfu_createBuffer(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return nfu_handle_obj(c, 0, "buffer");
    int bid = s_bu_n + 1; if (bid > 255) bid = 255;
    s_bu_n = bid;
    return nfu_handle_obj(c, bid, "buffer");
}

// bindBuffer(target, bid) - remember target for bufferData
static int s_bu_target[256];
// NEFU-FIX: bufferData used to locate its buffer by scanning s_bu_target for a
// matching GL target. Every ARRAY_BUFFER shares target 34962, so the scan always
// hit whichever buffer was created first and each later upload silently overwrote
// it - a page's vertex array was replaced by its colour array, leaving degenerate
// draws that reported GL_NO_ERROR and rendered nothing. Track the live binding
// instead (the command stream is one GL state machine, so last-bind is exact).
static int s_bu_bound_target = 0;
static int s_bu_bound_id = 0;
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
    s_bu_bound_target = (int)td;
    s_bu_bound_id = bid;
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
    if (s_bu_bound_target == (int)td) bid = s_bu_bound_id;
    if (!bid) for (int i = 1; i < 256; i++) if (s_bu_target[i] == (int)td) { bid = i; break; }
    // Big payload first: a typed array goes to the out-of-band pool, so the
    // upload is complete instead of the 160-number text fragment the legacy
    // path could carry.
    if (argc > 1 && JS_IsObject(argv[1]) && !JS_IsString(argv[1])) {
        int blen = 0;
        const uint8_t* bp = js_bin_of(c, argv[1], &blen);
        if (bp && blen > 0) {
            unsigned gid = nefu_glbin_put(bp, blen);
            if (gid) {
                char args[96];
                sprintf(args, "%d,%d,%u,%d", bid, (int)td, gid, blen);
                gl_emit_big(rc, "bufp", cid, args);
                return JS_UNDEFINED;
            }
        }
    }
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

// bufferSubData(target, offset, data) - patched slice of an existing buffer.
// Only the pool form is implemented: partial updates are exactly as large as
// full ones, so the legacy text fragment would be wrong by definition.
static JSValue nfu_bufferSubData(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    double td = 0, off = 0;
    if (argc > 0) JS_ToFloat64(c, &td, argv[0]);
    if (argc > 1) JS_ToFloat64(c, &off, argv[1]);
    int bid = 0;
    if (s_bu_bound_target == (int)td) bid = s_bu_bound_id;
    if (!bid) for (int i = 1; i < 256; i++) if (s_bu_target[i] == (int)td) { bid = i; break; }
    if (!bid) return JS_UNDEFINED;
    if (argc > 2) {
        int blen = 0;
        const uint8_t* bp = js_bin_of(c, argv[2], &blen);
        if (bp && blen > 0) {
            unsigned gid = nefu_glbin_put(bp, blen);
            if (gid) {
                char args[96];
                sprintf(args, "%d,%d,%d,%u,%d", bid, (int)td, (int)off, gid, blen);
                gl_emit_big(rc, "bufsubp", cid, args);
            }
        }
    }
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
    // A location must be an object too: babylon stores per-channel bookkeeping
    // on it (uniformLocation._currentState = channel) and keys its uniform map
    // by it, which throws "not an object" on a Number primitive.
    return nfu_handle_obj(c, uid, "uloc");
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
    return nfu_handle_obj(c, aid, "aloc");
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

static JSValue nfu_createTexture(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return nfu_handle_obj(c, 0, "texture");
    int tid = s_tx_n + 1; if (tid > 255) tid = 255;
    s_tx_n = tid;
    char args[32];
    sprintf(args, "%d", tid);
    gl_emit(rc, "tx", cid, args);
    return nfu_handle_obj(c, tid, "texture");
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

// isContextLost(): the software rasterizer never loses its context.
static JSValue nfu_ret_false(JSContext* c, JSValueConst thisv, int argc,
                             JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    return JS_NewBool(c, 0);
}

// Framebuffer objects are accepted but never bound or rendered into, so the
// status query always answers FRAMEBUFFER_COMPLETE (0x8CD5). Without this
// Babylon's render-target probe treated every target as broken.
static JSValue nfu_getFramebufferStatus(JSContext* c, JSValueConst thisv,
                                        int argc, JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    return JS_NewInt32(c, 0x8CD5);
}

// ---- WebGL context queries -------------------------------------------------
// A page that creates a WebGL context asks for capabilities before it draws
// anything. babylon (bloxd's 3D engine) starts with
//   gl.getExtension("WEBGL_debug_renderer_info")
//   gl.getParameter(gl.UNMASKED_RENDERER_WEBGL)
// and aborts the whole engine setup when those are missing - the game never
// loads its chunk and the page stays empty. So the context has to answer the
// standard capability queries, not just the drawing calls.

// getExtension(name): the software rasterizer exposes no real extensions, but
// the object still has to be an OBJECT carrying the extension's constants.
//
// Returning null here is what breaks bloxd's engine. Its GPU-probe helper
// does:
//     it = gl.getExtension("WEBGL_debug_renderer_info");
//     ... gl.getParameter(it.UNMASKED_RENDERER_WEBGL) ...
// and then, in a finally block:
//     Dg = it.getExtension("WEBGL_lose_context");  Dg.loseContext();
// The first call returns null, so `it` is null and `it.getExtension` throws
// "TypeError: not a function" - the engine setup aborts, the game chunk is
// never imported and the page stays empty.
//
// So: return an object carrying the extension's enum constants and the
// loseContext / restoreContext methods.
static JSValue nfu_getExtension(JSContext* c, JSValueConst thisv,
                                int argc, JSValueConst* argv) {
    (void)thisv;
    const char* nm = (argc > 0 && JS_IsString(argv[0])) ? JS_ToCString(c, argv[0]) : 0;
    if (!nm) return JS_NULL;
    JSValue o = JS_NULL;
    if (!strcmp(nm, "WEBGL_debug_renderer_info")) {
        o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "UNMASKED_VENDOR_WEBGL", JS_NewInt32(c, 0x9245));
        JS_SetPropertyStr(c, o, "UNMASKED_RENDERER_WEBGL", JS_NewInt32(c, 0x9246));
    } else if (!strcmp(nm, "WEBGL_lose_context")) {
        o = JS_NewObject(c);
        JS_SetPropertyStr(c, o, "loseContext", JS_NewCFunction(c, nfu_ret_false, "loseContext", 0));
        JS_SetPropertyStr(c, o, "restoreContext", JS_NewCFunction(c, nfu_ret_false, "restoreContext", 0));
    } else if (!strcmp(nm, "OES_element_index_uint")) {
        // Voxel meshes overflow 16-bit indices: one chunk easily exceeds 65536
        // vertices, so an engine that only sees UNSIGNED_SHORT either splits the
        // mesh into many draws or drops it entirely. The rasterizer already
        // decodes UNSIGNED_INT index buffers, so advertising the extension is
        // honest; it needs no extra entry points.
        o = JS_NewObject(c);
    }
    JS_FreeCString(c, nm);
    // null for every other extension. Handing back a generic object for
    // ANGLE_instanced_arrays made Babylon call drawArraysInstancedANGLE (a
    // method the bridge never had) on the WebGL1 path; null makes engines
    // cleanly skip the optional feature instead of crashing.
    return o;
}

// getSupportedExtensions(): the extensions the bridge actually answers. An
// empty list made every engine treat the context as extension-free, so the
// uint-index and VAO paths were disabled before getExtension was even probed.
static JSValue nfu_getSupportedExtensions(JSContext* c, JSValueConst thisv,
                                          int argc, JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    JSValue a = JS_NewArray(c);
    JS_SetPropertyUint32(c, a, 0, JS_NewString(c, "WEBGL_debug_renderer_info"));
    JS_SetPropertyUint32(c, a, 1, JS_NewString(c, "WEBGL_lose_context"));
    JS_SetPropertyUint32(c, a, 2, JS_NewString(c, "OES_element_index_uint"));
    return a;
}

// getParameter(pname): the renderer strings for the known string enums, 0
// for every numeric enum (limits are generous enough for the engine to run).
static JSValue nfu_getParameter(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    (void)thisv;
    int pname = 0;
    if (argc > 0 && JS_IsNumber(argv[0])) {
        int32_t v = 0;
        if (JS_ToInt32(c, &v, argv[0]) == 0) pname = (int)v;
    }
    // WebGL.VENDOR = 0x1F00, WebGL.RENDERER = 0x1F01
    if (pname == 0x1F00) return JS_NewString(c, "nefuOS");
    if (pname == 0x1F01) return JS_NewString(c, "nefuOS software rasterizer");
    // UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL
    if (pname == 0x9245) return JS_NewString(c, "nefuOS");
    if (pname == 0x9246) return JS_NewString(c, "nefuOS software rasterizer");
    // VERSION / SHADING_LANGUAGE_VERSION - GLES 2.0 (WebGL1) presentation
    if (pname == 0x1F02) return JS_NewString(c, "WebGL 1.0 (nefuOS OpenGL ES 2.0)");
    if (pname == 0x8B8C) return JS_NewString(c, "WebGL GLSL ES 1.00 (nefuOS)");
    // MAX_TEXTURE_SIZE / MAX_VIEWPORT_DIMS / MAX_RENDERBUFFER_SIZE
    if (pname == 0x0D33) return JS_NewInt32(c, 4096);
    if (pname == 0x0D3A) return JS_NewInt32(c, 4096);
    if (pname == 0x84E8) return JS_NewInt32(c, 4096);
    if (pname == 0x8869) return JS_NewInt32(c, 16);   // MAX_VERTEX_ATTRIBS
    if (pname == 0x8B4D) return JS_NewInt32(c, 256);  // MAX_COMBINED_TEXTURE_IMAGE_UNITS
    if (pname == 0x8073) return JS_NewInt32(c, 16);   // MAX_TEXTURE_IMAGE_UNITS
    // 0x851C is MAX_CUBE_MAP_TEXTURE_SIZE, not the anisotropy limit. Returning
    // 32 here made engines clamp every cube map (skybox, shadow atlas) to 32px.
    if (pname == 0x851C) return JS_NewInt32(c, 2048); // MAX_CUBE_MAP_TEXTURE_SIZE
    if (pname == 0x84FF) return JS_NewInt32(c, 16);   // MAX_TEXTURE_MAX_ANISOTROPY_EXT
    if (pname == 0x8B4C) return JS_NewInt32(c, 16);   // MAX_VERTEX_TEXTURE_IMAGE_UNITS
    // 0x8872 is MAX_TEXTURE_IMAGE_UNITS (per-fragment), not the vertex one:
    // answering the OES enum with this value returned 0 for
    // MAX_VERTEX_TEXTURE_IMAGE_UNITS, and Babylon then declares vertex texture
    // sampling unavailable and refuses the blocks that need it.
    if (pname == 0x8872) return JS_NewInt32(c, 16);   // MAX_TEXTURE_IMAGE_UNITS
    // GLES 2.0 minimum vector limits (engines branch on these for shader
    // complexity; 0 would make them disable features they can render)
    if (pname == 0x8DFB) return JS_NewInt32(c, 128);  // MAX_VERTEX_UNIFORM_VECTORS
    if (pname == 0x8DFC) return JS_NewInt32(c, 15);   // MAX_VARYING_VECTORS
    if (pname == 0x8DFD) return JS_NewInt32(c, 16);   // MAX_FRAGMENT_UNIFORM_VECTORS
    return JS_NewInt32(c, 0);
}

// getContextAttributes(): the subset of the real object the engine reads.
static JSValue nfu_getContextAttributes(JSContext* c, JSValueConst thisv,
                                        int argc, JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "alpha", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "antialias", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "depth", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "stencil", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "premultipliedAlpha", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "preserveDrawingBuffer", JS_NewBool(c, 1));
    JS_SetPropertyStr(c, o, "powerPreference", JS_NewString(c, "default"));
    return o;
}

// getShaderPrecisionFormat(): the numbers the engine uses to pick a shader
// format. Highp everywhere is the safe answer for a software rasterizer.
static JSValue nfu_getShaderPrecisionFormat(JSContext* c, JSValueConst thisv,
                                             int argc, JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "rangeMin", JS_NewInt32(c, 127));
    JS_SetPropertyStr(c, o, "rangeMax", JS_NewInt32(c, 127));
    JS_SetPropertyStr(c, o, "precision", JS_NewInt32(c, 23));
    return o;
}

// getProgramParameter(): LINK_STATUS / VALIDATE_STATUS are booleans, but the
// count queries (ACTIVE_UNIFORMS, ACTIVE_ATTRIBUTES, ATTACHED_SHADERS, ...)
// must be NUMBERS. The old bridge returned `true` for everything, and
// `for (let i = 0; i < count; i++) getActiveUniform(...)` with count === true
// loops once and dereferences introspection data the bridge never had.
// Reporting 0 active resources is the safe WebGL1 answer: engines skip
// introspection and drive uniforms through the names they already resolved.
static JSValue nfu_getProgramParameter(JSContext* c, JSValueConst thisv,
                                        int argc, JSValueConst* argv) {
    (void)thisv;
    // getProgramParameter(program, pname): the pname is the SECOND argument.
    // Reading argv[0] here silently answered the program id instead, so every
    // LINK_STATUS query came back 0 and engines treated a good link as a
    // failure.
    int pname = 0;
    if (argc > 1 && JS_IsNumber(argv[1])) {
        int32_t v = 0;
        if (JS_ToInt32(c, &v, argv[1]) == 0) pname = v;
    }
    if (pname == 0x8B82 || pname == 0x8B83) {
        static int s_n1 = 0;
        if (s_n1 < 24) { s_n1++; fprintf(stderr, "\n[GPP] pname=0x%x argc=%d -> 1\n", pname, argc); fflush(stderr); }
        return JS_NewBool(c, 1);  // LINK_STATUS / VALIDATE_STATUS
    }
    if (pname == 0x8B80) return JS_NewBool(c, 0);                     // DELETE_STATUS
    {
        static int s_n2 = 0;
        if (s_n2 < 24) { s_n2++; fprintf(stderr, "\n[GPP] pname=0x%x argc=%d -> 0 (count)\n", pname, argc); fflush(stderr); }
    }
    return JS_NewInt32(c, 0);   // counts
}

// getShaderParameter(): COMPILE_STATUS true, DELETE_STATUS false, counts 0.
static JSValue nfu_getShaderParameter(JSContext* c, JSValueConst thisv,
                                       int argc, JSValueConst* argv) {
    (void)thisv;
    // getShaderParameter(shader, pname): pname is the SECOND argument (same
    // argv[0] mistake the program variant had).
    int pname = 0;
    if (argc > 1 && JS_IsNumber(argv[1])) {
        int32_t v = 0;
        if (JS_ToInt32(c, &v, argv[1]) == 0) pname = v;
    }
    if (pname == 0x8B81) return JS_NewBool(c, 1);   // COMPILE_STATUS
    if (pname == 0x8B80) return JS_NewBool(c, 0);   // DELETE_STATUS
    return JS_NewInt32(c, 0);
}

// getActiveAttrib / getActiveUniform stub: empty name, zero size. Only
// reached when an engine ignores the 0 count from getProgramParameter.
static JSValue nfu_getActive(JSContext* c, JSValueConst thisv,
                              int argc, JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "name", JS_NewString(c, ""));
    JS_SetPropertyStr(c, o, "type", JS_NewInt32(c, 0x1406));   // FLOAT
    JS_SetPropertyStr(c, o, "size", JS_NewInt32(c, 0));
    return o;
}

// uniformNfv / uniformNiv (loc, values): the array form engines use for
// light/bone packs. The wire only carries one uniform's worth of data per
// proxy location, so the first N components are forwarded through the
// existing uniformNf path (arrays longer than N would need indexed uniform
// slots, which the interpreter does not model yet; writing name[0] is the
// graceful degradation, never a crash).
static JSValue nfu_uniformv(JSContext* c, JSValueConst thisv, int argc,
                             JSValueConst* argv, int magic) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    char* cid = gl_cid_of(c, thisv);
    if (!cid) return JS_UNDEFINED;
    int n = magic;   // 1..4 components
    double d = 0; if (argc > 0) JS_ToFloat64(c, &d, argv[0]);
    char args[400];
    int ap = sprintf(args, "%d", (int)d);
    int got = 0;
    if (argc > 1) got = append_nums(c, argv[1], args, ap, (int)sizeof(args), n);
    for (int i = got; i < n; i++) ap += sprintf(args + ap, ",0");
    static const char* const nm[4] = { "u1f", "u2f", "u3f", "u4f" };
    gl_emit(rc, nm[n - 1], cid, args);
    return JS_UNDEFINED;
}

// readPixels(): the engine reads back the framebuffer for picking and
// post-processing. Fill the requested rectangle with zeroes.
static JSValue nfu_readPixels(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    (void)thisv;
    if (argc < 7) return JS_UNDEFINED;
    int64_t w = 0, h = 0;
    JS_ToInt64(c, &w, argv[2]);
    JS_ToInt64(c, &h, argv[3]);
    JSValue dst = argv[6];
    if (w <= 0 || h <= 0 || !JS_IsArray(c, dst)) return JS_UNDEFINED;
    // Uint8Array has a length property and indexed stores; fill through it
    JSValue lenv = JS_GetPropertyStr(c, dst, "length");
    uint32_t len = 0;
    if (JS_IsNumber(lenv)) { int32_t lv = 0; if (JS_ToInt32(c, &lv, lenv) == 0) len = (uint32_t)lv; }
    uint32_t want = (uint32_t)(w * h * 4);
    if (len < want) want = len;
    for (uint32_t i = 0; i < want; i++) {
        JS_SetPropertyUint32(c, dst, i, JS_NewInt32(c, 0));
    }
    return JS_UNDEFINED;
}

// deleteShader / deleteProgram / deleteBuffer / deleteTexture /
// deleteVertexArray: the engine frees resources on teardown. Nothing to do.
static JSValue nfu_gl_delete(JSContext* c, JSValueConst thisv, int argc,
                             JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    return JS_UNDEFINED;
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
    {
        // A DOM sync that never returns is indistinguishable from a dead page
        // from the outside; bracket the string conversion so the watchdog's
        // "last_c" tag can be resolved to "between these two lines".
        static int s_sync_n = 0;
        if (s_sync_n < 8) {
            s_sync_n++;
            klogf("minijs: sync enter #%d argc=%d buf=%d", s_sync_n, argc, rc ? rc->html_sz : 0);
        }
    }
    if (rc && rc->html && rc->html_sz > 1) {
        rc->hi = 0;
        if (argc > 0 && JS_IsString(argv[0])) {
            size_t L = 0;
            const char* h = JS_ToCStringLen(c, &L, argv[0]);
            if (h) {
                static int s_sync_len_n = 0;
                if (s_sync_len_n < 8) {
                    s_sync_len_n++;
                    klogf("minijs: sync got len=%u", (unsigned)L);
                }
                if (L >= (size_t)rc->html_sz - 1) {
                    // Silent truncation used to look like a shim bug: the page
                    // re-rendered with a body cut mid-tag. Say it out loud.
                    static int s_sync_trunc_logged = 0;
                    L = (size_t)rc->html_sz - 1;
                    if (s_sync_trunc_logged++ < 3)
                        klogf("minijs: WARNING DOM sync output TRUNCATED (%d byte buffer), page body cut",
                              rc->html_sz);
                }
                memcpy(rc->html, h, L);
                rc->hi = (int)L;
                JS_FreeCString(c, h);
            }
        }
        rc->html[rc->hi] = 0;
    }
    return JS_UNDEFINED;
}

// Global-scope eval for dynamically injected classic scripts: the DOM shim
// fetches runtime <script src> targets and evals them here so top-level
// var/function declarations land on window exactly like a real <script>.
// Returns "" on success, the exception message on failure (the shim turns a
// non-empty result into a script onerror).
// Extend the run deadline from JS: an iframe challenge callback (or any
// long timer continuation) started after the page's worker budget was
// exhausted would otherwise be interrupted at every single JS call — even
// the catch() logging threw, making the stall look like total silence.
static JSValue nfu_extendDeadline(JSContext* c, JSValueConst thisv,
                                  int argc, JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    if (!rc) return JS_UNDEFINED;
    int64_t ext_s = 60;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_ToInt64(c, &ext_s, argv[0])) { JS_GetException(c); return JS_UNDEFINED; }
    }
    if (ext_s < 1) ext_s = 1;
    if (ext_s > 3600) ext_s = 3600;
    uint32_t floor_dl = platform_tick_ms() + (uint32_t)ext_s * 1000;
    if (rc->deadline < floor_dl) rc->deadline = floor_dl;
    return JS_UNDEFINED;
}

// ---- native UTF-8 codec ----------------------------------------------------
// The shim's TextEncoder/TextDecoder used to walk the string one charCode at a
// time from JS. That is fine for a fetched JSON body but hopeless for a bloxd
// room message: the per-instruction interrupt budget expired in the middle of a
// multi-hundred-KB WebSocket payload, QuickJS reported "InternalError:
// interrupted", and the send was lost. QuickJS already stores JS strings as
// UTF-8 internally, so both directions can be a single native conversion.
//
// __nfuUtf8Enc(s) -> ArrayBuffer of UTF-8 bytes
// __nfuUtf8Dec(view) -> string, decoding ArrayBuffer/typed-array bytes
static JSValue nfu_utf8enc(JSContext* c, JSValueConst thisv, int argc,
                           JSValueConst* argv) {
    if (argc < 1) return JS_NewArrayBufferCopy(c, (const uint8_t*)"", 0);
    size_t len = 0;
    // cesu8=false makes QuickJS emit (and accept) real UTF-8, which is what
    // TextEncoder must produce; lone surrogates become U+FFFD, same as the spec.
    const char* s = JS_ToCStringLen(c, &len, argv[0]);
    if (!s) { JS_GetException(c); return JS_NewArrayBufferCopy(c, (const uint8_t*)"", 0); }
    JSValue ab = JS_NewArrayBufferCopy(c, (const uint8_t*)s, len);
    JS_FreeCString(c, s);
    return ab;
}

static JSValue nfu_utf8dec(JSContext* c, JSValueConst thisv, int argc,
                           JSValueConst* argv) {
    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) return JS_NewString(c, "");
    size_t off = 0, blen = 0, bpe = 1;
    uint8_t* p = NULL;
    size_t n = 0;
    int is_view = 0;
    JSValue ab = JS_GetTypedArrayBuffer(c, argv[0], &off, &blen, &bpe);
    if (!JS_IsException(ab)) {
        is_view = 1;
        p = JS_GetArrayBuffer(c, &n, ab);
        JS_FreeValue(c, ab);
    } else {
        JS_GetException(c);   // not a typed array: clear and try a plain buffer
        p = JS_GetArrayBuffer(c, &n, argv[0]);
        off = 0;
        blen = n;
    }
    (void)bpe;
    // A view is a window into a larger buffer, and the window is the whole
    // point: glTF's header parser hands TextDecoder the JSON chunk as
    // `bytes.subarray(20, 20 + jsonLen)`. Decoding the backing buffer instead
    // of the window prefixed the chunk with the "glTF" magic and every model
    // died in JSON.parse ("GLB refused: it is not a binary glTF 2.0 file").
    (void)is_view;
    if (!p) {
        JS_GetException(c);
        return JS_NewString(c, "");
    }
    if (off > n) { off = n; blen = 0; }
    if (off + blen > n) blen = n - off;
    return JS_NewStringLen(c, (const char*)p + off, blen);
}

static JSValue nfu_evalGlobal(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(c, "");
    size_t L = 0;
    const char* code = JS_ToCStringLen(c, &L, argv[0]);
    if (!code) { JS_GetException(c); return JS_NewString(c, "code not a string"); }
    const char* fn = (argc > 1 && JS_IsString(argv[1])) ? JS_ToCString(c, argv[1]) : NULL;
    // Dynamic scripts eval inside timer callbacks, whose 250 ms tick frame
    // budget killed ad SDK bundles mid-parse ("InternalError: interrupted").
    // Extend the deadline for this eval like the module loader does, never
    // shortening a longer-running budget, and restore it afterwards.
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    uint32_t saved_dl = rc ? rc->deadline : 0;
    if (rc) {
        // optional argv[2]: extension seconds (iframe challenge scripts need
        // far more than the default 30s when the page already burned budget)
        uint32_t ext_s = 30;
        if (argc > 2) {
            int64_t v = 30;
            if (!JS_ToInt64(c, &v, argv[2])) {
                if (v > 5 && v < 3600) ext_s = (uint32_t)v;
            } else {
                JS_GetException(c);
            }
        }
        uint32_t floor_dl = platform_tick_ms() + ext_s * 1000;
        if (rc->deadline < floor_dl) rc->deadline = floor_dl;
    }
    JSValue rv = JS_Eval(c, code, L, (fn && fn[0]) ? fn : "<script src>",
                         JS_EVAL_TYPE_GLOBAL);
    if (rc) rc->deadline = saved_dl;
    JS_FreeCString(c, code);
    if (fn) JS_FreeCString(c, fn);
    if (!JS_IsException(rv)) {
        JS_FreeValue(c, rv);
        return JS_NewString(c, "");
    }
    // drain first (memory rule), then stringify the exception for the shim
    JSValue exc = JS_GetException(c);
    char buf[512];
    if (!JS_IsError(c, exc) && !JS_IsString(exc)) {
        JSValue s = JS_ToString(c, exc);
        JS_FreeValue(c, exc);
        exc = s;
    }
    const char* msg = JS_ToCString(c, exc);
    snprintf(buf, sizeof(buf), "%s", msg ? msg : "exception");
    // A message like "not a function (got undefined)" names nothing: without
    // the call site there is no way to tell which shim API a page wanted.
    // Print the QuickJS stack (file:line:col per frame) alongside it.
    if (!JS_IsString(exc)) {
        JSValue st = JS_GetPropertyStr(c, exc, "stack");
        if (!JS_IsException(st) && !JS_IsUndefined(st) && !JS_IsNull(st)) {
            const char* ss = JS_ToCString(c, st);
            if (ss && ss[0]) {
                klogf("js: [STACK] %.400s", ss);
                JS_FreeCString(c, ss);
            }
        }
        JS_FreeValue(c, st);
    }
    JS_FreeCString(c, msg);
    JS_FreeValue(c, exc);
    return JS_NewString(c, buf);
}

// ---- bitmap pixel bridge (bloxd's atlas pipeline) ----------------------
// bloxd builds its block/armour atlases in JS: decodeImage() feeds a 2D
// canvas, drawImage() rotates the tile, getImageData() reads the bytes back.
// The shim cannot decode anything itself, so this bridge hands it real RGBA
// pixels: fetch (or data: URI) -> stb decode -> base64 -> "W,H,<b64>".
// Base64 is the transfer format because JS_NewStringLen would corrupt raw
// binary (UTF-8 replacement) and an ArrayBuffer hand-off is not available on
// this path.
static const char* kB64Tbl =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// data:image/...;base64,xxxx -> raw bytes (kalloc'd, caller kfrees)
static uint8_t* nfuDataUrlBytes(const char* uri, uint32_t* out_len) {
    *out_len = 0;
    const char* comma = strchr(uri, ',');
    if (!comma) return 0;
    if (!strstr(uri, ";base64")) return 0;   // percent-encoded data URLs are not used here
    const char* p = comma + 1;
    uint32_t n = (uint32_t)strlen(p);
    uint32_t cap = n / 4 * 3 + 4;
    uint8_t* out = (uint8_t*)kalloc(cap);
    if (!out) return 0;
    uint32_t o = 0;
    int acc = 0, bits = 0;
    for (uint32_t i = 0; i < n; i++) {
        int ch = (unsigned char)p[i];
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+') v = 62;
        else if (ch == '/') v = 63;
        else if (ch == '=') break;
        else continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    *out_len = o;
    return out;
}

static JSValue nfu_bitmapPixels(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(c, "");
    size_t L = 0;
    const char* u = JS_ToCStringLen(c, &L, argv[0]);
    if (!u) return JS_NewString(c, "");
    char* url = (char*)malloc(L + 1);
    if (!url) { JS_FreeCString(c, u); return JS_NewString(c, ""); }
    memcpy(url, u, L); url[L] = 0;
    JS_FreeCString(c, u);

    uint8_t* body = 0;
    uint32_t blen = 0;
    bool own_body = false;
    if (strncmp(url, "data:", 5) == 0) {
        body = nfuDataUrlBytes(url, &blen);
        own_body = true;
    } else if (strncmp(url, "http", 4) == 0) {
        if (platform_http_get(url, &body, &blen)) own_body = true;
    }
    free(url);
    if (!body || blen == 0) {
        if (body && own_body) kfree(body);
        fprintf(stderr, "[BITMAP] fetch failed\n");
        return JS_NewString(c, "");
    }

    int w = 0, h = 0;
    uint8_t* rgba = 0;
    bool ok = nefu::stbi_info_mem(body, (int)blen, &w, &h) &&
              w > 0 && h > 0 && (long)w * h <= 1500000L &&
              nefu::stbi_decode_mem(body, (int)blen, &w, &h, &rgba);
    if (own_body) kfree(body);
    if (!ok || !rgba) {
        if (rgba) kfree(rgba);
        fprintf(stderr, "[BITMAP] decode failed\n");
        return JS_NewString(c, "");
    }

    uint32_t n = (uint32_t)w * (uint32_t)h * 4u;
    uint32_t b64cap = (n + 2) / 3 * 4 + 8;
    char* b64 = (char*)malloc(b64cap);
    if (!b64) { kfree(rgba); return JS_NewString(c, ""); }
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)rgba[i] << 16;
        if (i + 1 < n) v |= (uint32_t)rgba[i + 1] << 8;
        if (i + 2 < n) v |= (uint32_t)rgba[i + 2];
        b64[o++] = kB64Tbl[(v >> 18) & 63];
        b64[o++] = kB64Tbl[(v >> 12) & 63];
        b64[o++] = (i + 1 < n) ? kB64Tbl[(v >> 6) & 63] : '=';
        b64[o++] = (i + 2 < n) ? kB64Tbl[v & 63] : '=';
    }
    b64[o] = 0;
    kfree(rgba);

    char head[48];
    int hn = snprintf(head, sizeof(head), "%d,%d,", w, h);
    JSValue r = JS_NewStringLen(c, head, (size_t)hn);
    // Concatenate header + base64 without a second full copy of the payload:
    // QuickJS owns the final string, so build it once from two parts.
    JS_FreeValue(c, r);
    {
        size_t total = (size_t)hn + (size_t)o;
        char* all = (char*)malloc(total + 1);
        if (!all) { free(b64); return JS_NewString(c, ""); }
        memcpy(all, head, (size_t)hn);
        memcpy(all + hn, b64, (size_t)o);
        all[total] = 0;
        r = JS_NewStringLen(c, all, total);
        free(all);
    }
    free(b64);
    return r;
}

// Some asset URLs a page hands to a worker are built from a publicPath that
// only exists in the page's own deploy. bloxd's /play bundle asks for
// /static/media/draco_wasm_wrapper_gltf_v2_1.<hash>.js and the same for the
// decoder .wasm, but bloxd.io serves those two files only at
// /static/draco_wasm_wrapper_gltf_v2_1.js and /static/draco_decoder_gltf_v2_1.wasm
// (the wrong paths answer 200 with the SPA's index.html, so a request "succeeds"
// and the caller then explodes on '<'). Rewrite them to the real location,
// keeping whichever origin the page is on.
static void nfu_alias_asset_url(const char* url, char* out, size_t cap) {
    out[0] = 0;
    if (!url || !cap) return;
    const char* canon = NULL;
    if (strstr(url, "draco_wasm_wrapper_gltf_v2_1")) canon = "/static/draco_wasm_wrapper_gltf_v2_1.js";
    else if (strstr(url, "draco_decoder_gltf_v2_1")) canon = "/static/draco_decoder_gltf_v2_1.wasm";
    if (!canon) { snprintf(out, cap, "%s", url); return; }
    const char* sep = strstr(url, "://");
    if (!sep) { snprintf(out, cap, "https://bloxd.io%s", canon); return; }
    const char* host_start = sep + 3;
    const char* slash = strchr(host_start, '/');
    size_t origin_len = slash ? (size_t)(slash - url) : strlen(url);
    if (origin_len + 2 >= cap) origin_len = cap - 2;
    snprintf(out, cap, "%.*s%s", (int)origin_len, url, canon);
}

// synchronous HTTP GET exposed to the XHR shim: returns the response body
// (local overrides + chunk cache make bloxd's resource loads fast)
static JSValue nfu_httpGet(JSContext* c, JSValueConst thisv, int argc,
                           JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewString(c, "");
    size_t L = 0;
    const char* u = JS_ToCStringLen(c, &L, argv[0]);
    if (!u) return JS_NewString(c, "");
    char* url = (char*)malloc(L + 1);
    if (!url) { JS_FreeCString(c, u); return JS_NewString(c, ""); }
    memcpy(url, u, L); url[L] = 0;
    JS_FreeCString(c, u);
    // NEFU_BLOCK_DRACO is a diagnostic switch: draco's decoder can spin forever
    // inside wasm3, and answering its wrapper script with an empty body takes
    // that path out of the page so the rest of the boot is observable.
    if (getenv("NEFU_BLOCK_DRACO") && strstr(url, "draco")) {
        fprintf(stderr, "\n[NODRACO] blocked %s\n", url);
        fflush(stderr);
        free(url);
        return JS_NewString(c, "");
    }
    uint8_t* body = 0; uint32_t blen = 0;
    char alias[1024];
    nfu_alias_asset_url(url, alias, sizeof(alias));
    bool got = platform_http_get(alias, &body, &blen);
    JSValue r;
    if (got && body && blen > 0) {
        r = JS_NewStringLen(c, (const char*)body, blen);
        kfree(body);
    } else {
        r = JS_NewString(c, "");
    }
    free(url);
    return r;
}

// True when a response body must not be turned into a JS string. quickjs's
// JS_NewStringLen decodes UTF-8 and replaces every byte it cannot decode with
// U+FFFD, so an image / wasm module / font fetched through XHR would arrive
// corrupted (and twice its size, since the string becomes 16-bit). Those
// payloads are handed to JS as an ArrayBuffer instead; everything textual -
// JSON, HTML, JS, XML, SVG, CSV, form bodies - keeps the plain string path it
// has always used.
static bool nfu_body_looks_text(const uint8_t* p, uint32_t len) {
    // Content-Type is a hint, not the truth: bloxd posts to /images/<Block>.png
    // and the origin answers with JSON while still labelling it as an image type,
    // which used to route the body through the binary path (body = "") and blow up
    // the caller's JSON.parse. sniff the first bytes so a JSON/HTML payload is
    // always readable as text no matter what the header claims.
    if (!p || len == 0) return false;
    // Container magic first: a .glb opens with ASCII "glTF", so the letter test
    // below routed every model through the text path and the GLB parser then
    // rejected the decoded string ("not a binary glTF 2.0 file"). Same shape for
    // images, fonts and audio, which all start with a printable-looking tag.
    if (len >= 4) {
        if (p[0] == 'g' && p[1] == 'l' && p[2] == 'T' && p[3] == 'F') return false;   // glTF binary
        if (p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F') return false;   // WEBP/WAV
        if (p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S') return false;   // Ogg
        if (p[0] == 'f' && p[1] == 'L' && p[2] == 'a' && p[3] == 'C') return false;   // FLAC
        if (p[0] == 'P' && p[1] == 'K' && p[2] == 3 && p[3] == 4) return false;       // zip
        if (p[0] == 'I' && p[1] == 'D' && p[2] == '3') return false;                  // mp3
        if (p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') return false;  // PNG
        if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return false;               // JPEG
        if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && p[3] == 0x00) return false;// ICO
        if (p[0] == 'D' && p[1] == 'D' && p[2] == 'S' && p[3] == ' ') return false;   // DDS
        if (p[0] == 0xAB && p[1] == 'K' && p[2] == 'T' && p[3] == 'X') return false;  // KTX
    }
    // A NUL byte inside the head of the payload is the cheapest honest signal
    // that this is not UTF-8 text, whatever the letter at offset 0 suggests.
    uint32_t nul_at = len < 512 ? len : 512;
    for (uint32_t q = 0; q < nul_at; q++) if (p[q] == 0) return false;
    uint32_t i = 0;
    while (i < len && (p[i] == '\xef' || p[i] == '\xbb' || p[i] == '\xbf' || p[i] == ' ' ||
                       p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;   // BOM + leading space
    if (i >= len) return false;
    uint8_t c = p[i];
    if (c == '{' || c == '[' || c == '"' || c == '<') return true;
    if ((c >= '0' && c <= '9') || c == '-') return true;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    return false;
}
static bool nfu_body_is_binary(const char* hdrs) {
    if (!hdrs) return false;
    static const char* kTexty[] = { "text/", "json", "javascript", "ecmascript",
                                    "xml", "html", "x-www-form-urlencoded",
                                    "svg", "csv", "plain", "graphql",
                                    "event-stream" };
    for (const char* p = hdrs; *p; ) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        int ln = (int)(e - p);
        if (ln > 13) {
            static const char kCt[] = "content-type:";
            bool is_ct = true;
            for (int i = 0; i < 13 && is_ct; i++) {
                char a = p[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (a != kCt[i]) is_ct = false;
            }
            if (is_ct) {
                const char* v = p + 13;
                while (v < e && (*v == ' ' || *v == '\t')) v++;
                int n = (int)(e - v);
                // Substring, not prefix: the textual marker sits in the subtype
                // ("application/json", "application/javascript", "image/svg+xml").
                for (size_t t = 0; t < sizeof(kTexty) / sizeof(kTexty[0]); t++) {
                    int tl = (int)strlen(kTexty[t]);
                    for (int i = 0; i + tl <= n; i++) {
                        bool hit = true;
                        for (int k = 0; k < tl && hit; k++) {
                            char a = v[i + k];
                            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                            if (a != kTexty[t][k]) hit = false;
                        }
                        if (hit) return false;
                    }
                }
                return true;
            }
        }
        p = *e ? e + 1 : e;
    }
    return false;
}

// Cloudflare Turnstile cannot clear inside this engine: its fingerprinting
// needs 2D canvas / WebGL surfaces this browser does not provide yet, so the
// widget never yields a token and the client falls back to a literal "none".
// bloxd then rejects the world join with a bare pong, which leaves the game
// stuck on its loading screen no matter how correct the rest of the engine is.
// A traffic code obtained elsewhere on this same machine is a signed blob that
// stays valid for hours, so until Turnstile itself works the flow can be
// exercised by serving the POST that fetches it from a file:
//   NEFU_TRAFFIC_CODE_FILE=<path>  (default: traffic_code.txt in cwd)
static const char* nfu_traffic_code(void) {
    static char s_code[4096];
    static int s_loaded = 0;
    if (!s_loaded) {
        s_loaded = 1;
        const char* path = getenv("NEFU_TRAFFIC_CODE_FILE");
        if (!path || !*path) path = "traffic_code.txt";
        FILE* f = fopen(path, "rb");
        if (f) {
            size_t n = fread(s_code, 1, sizeof(s_code) - 1, f);
            fclose(f);
            while (n > 0 && (s_code[n - 1] == '\n' || s_code[n - 1] == '\r' || s_code[n - 1] == ' ')) n--;
            s_code[n] = 0;
            if (!s_code[0]) return NULL;
            fprintf(stderr, "\n[TC-LOAD] %s chars=%u\n", path, (unsigned)n);
            fflush(stderr);
        }
    }
    return s_code[0] ? s_code : NULL;
}

// Exposed to the page shim: when a traffic code is already on disk the client
// must not wait for the Turnstile widget at all, so localStorage is seeded
// with a fresh "no refresh needed" timestamp before any page script runs.
// Native TypedArray.prototype.copyWithin. Draco's decode wrapper copies slices
// of the wasm heap (hundreds of KB) through copyWithin; QuickJS has no builtin
// and the site's polyfill runs as an interpreted per-element loop, which held
// the page for minutes with no progress. memmove makes it O(bytes).
// Generic fallback: bloxd also calls copyWithin on array-likes (its own
// polyfill chain hands over an object, not a typed array), and QuickJS'
// JS_GetTypedArrayBuffer answers TypeError: not a TypedArray. Copy those
// element by element instead of failing the wasm import that called it.
static JSValue nfu_copywithin_generic(JSContext* c, JSValueConst thisv,
                                      int64_t t, int64_t s, int64_t e) {
    static int g_calls = 0;
    JSValue lv = JS_GetPropertyStr(c, thisv, "length");
    int64_t n = 0;
    JS_ToInt64(c, &n, lv);
    JS_FreeValue(c, lv);
    if (t < 0) t += n;
    if (s < 0) s += n;
    if (e < 0) e += n;
    if (t < 0) t = 0;
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    if (t > n) t = n;
    if (s > n) s = n;
    if (e > n) e = n;
    int64_t cnt = e - s;
    if (cnt > n - t) cnt = n - t;
    if (cnt <= 0) return JS_DupValue(c, thisv);
    g_calls++;
    if (getenv("NF_TA_TRACE") && (g_calls < 30 || (g_calls % 500) == 0))
        fprintf(stderr, "[TAGEN] call=%d len=%lld t=%lld s=%lld e=%lld cnt=%lld\n",
                g_calls, (long long)n, (long long)t, (long long)s, (long long)e,
                (long long)cnt);
    // Snapshot the source first so overlapping ranges behave like memmove.
    JSValue* tmp = (JSValue*)malloc(sizeof(JSValue) * (size_t)cnt);
    if (!tmp) return JS_DupValue(c, thisv);
    for (int64_t i = 0; i < cnt; i++)
        tmp[i] = JS_GetPropertyUint32(c, thisv, (uint32_t)(s + i));
    for (int64_t i = 0; i < cnt; i++) {
        JS_SetPropertyUint32(c, thisv, (uint32_t)(t + i), tmp[i]);
        JS_FreeValue(c, tmp[i]);
    }
    free(tmp);
    return JS_DupValue(c, thisv);
}

static JSValue nfu_ta_copywithin(JSContext* c, JSValueConst thisv, int argc,
                                 JSValueConst* argv) {
    // The shim calls this as g.__nfuTACopyWithin(this, t, s, e), so the receiver
    // is the global object and the array arrives as argv[0]; reading `this` here
    // silently copied nothing. Keep everything on argv.
    (void)thisv;
    if (argc < 1) return JS_UNDEFINED;
    JSValueConst obj = argv[0];
    size_t off = 0, len = 0, esz = 0;
    JSValue ab = JS_GetTypedArrayBuffer(c, obj, &off, &len, &esz);
    if (JS_IsException(ab)) {
        JS_FreeValue(c, JS_GetException(c));
        int64_t n = 0;
        JSValue lv = JS_GetPropertyStr(c, obj, "length");
        JS_ToInt64(c, &n, lv);
        JS_FreeValue(c, lv);
        int64_t t = 0, s = 0, e = n;
        if (argc > 1) JS_ToInt64(c, &t, argv[1]);
        if (argc > 2) JS_ToInt64(c, &s, argv[2]);
        if (argc > 3 && !JS_IsUndefined(argv[3])) JS_ToInt64(c, &e, argv[3]);
        return nfu_copywithin_generic(c, obj, t, s, e);
    }
    // JS_GetArrayBuffer dereferences its size out-parameter unconditionally,
    // so passing NULL there crashes the process; it needs a real slot.
    size_t absz = 0;
    uint8_t* data = JS_GetArrayBuffer(c, &absz, ab);
    int64_t n = esz ? (int64_t)(len / esz) : 0;
    int64_t t = 0, s = 0, e = n;
    if (argc > 1) JS_ToInt64(c, &t, argv[1]);
    if (argc > 2) JS_ToInt64(c, &s, argv[2]);
    if (argc > 3 && !JS_IsUndefined(argv[3])) JS_ToInt64(c, &e, argv[3]);
    if (t < 0) t += n;
    if (s < 0) s += n;
    if (e < 0) e += n;
    if (t < 0) t = 0;
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    if (t > n) t = n;
    if (s > n) s = n;
    if (e > n) e = n;
    int64_t cnt = e - s;
    if (cnt > n - t) cnt = n - t;
    if (data && cnt > 0 && esz)
        memmove(data + off + (size_t)t * esz, data + off + (size_t)s * esz,
                (size_t)cnt * esz);
    if (getenv("NF_TA_TRACE"))
        fprintf(stderr, "[TACW] off=%d len=%d esz=%d data=%d n=%lld t=%lld s=%lld e=%lld cnt=%lld\n",
                (int)off, (int)len, (int)esz, data ? 1 : 0, (long long)n,
                (long long)t, (long long)s, (long long)e, (long long)cnt);
    JS_FreeValue(c, ab);
    return JS_DupValue(c, obj);
}

static JSValue nfu_trafficSkip(JSContext* c, JSValueConst thisv, int argc,
                               JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    return JS_NewInt32(c, nfu_traffic_code() ? 1 : 0);
}

// Cookie jar seed. Colyseus room auth runs on the WebSocket *upgrade* request,
// which only carries whatever the document jar holds. Bloxd's game servers
// check the analytics visitor id (___Secure-3PSIDMC / 3PSIDMCPP) that the site's
// own JS mirror normally populates; a fabricated one is rejected by the server
// with a bare "onAuth failed" (error 4215) and the whole world join dies there.
// A cookie string obtained from a real browser session on this machine lets the
// engine reuse that identity:
//   NEFU_COOKIE_FILE=<path>   (default: cookies.txt in cwd)
// The text format is exactly document.cookie: "k=v; k2=v2; ...".
static const char* nfu_cookie_seed(void) {
    static char s_jar[8192];
    static int s_loaded = 0;
    if (!s_loaded) {
        s_loaded = 1;
        const char* path = getenv("NEFU_COOKIE_FILE");
        if (!path || !*path) path = "cookies.txt";
        FILE* f = fopen(path, "rb");
        if (f) {
            size_t n = fread(s_jar, 1, sizeof(s_jar) - 1, f);
            fclose(f);
            while (n > 0 && (s_jar[n - 1] == '\n' || s_jar[n - 1] == '\r')) n--;
            s_jar[n] = 0;
            if (!s_jar[0]) return NULL;
            fprintf(stderr, "\n[CK-LOAD] %s chars=%u\n", path, (unsigned)n);
            fflush(stderr);
        }
    }
    return s_jar[0] ? s_jar : NULL;
}

static JSValue nfu_cookieSeed(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    const char* s = nfu_cookie_seed();
    return s ? JS_NewString(c, s) : JS_NewString(c, "");
}

// Anonymous identity blob. POST /index/metrics/cookies returns the account the
// site mints for this visitor: a base64 JSON carrying 3PSIDMCPP, the starter
// player name and cosmetics. That blob is the credential every later
// /social/bloxd-matchmake call is checked against, so it cannot be fabricated -
// without it the server answers a bare 401. A blob captured from a real browser
// session on this machine, paired with the same cookie jar, lets the engine
// reuse the identity:
//   NEFU_METRICS_FILE=<path>   (default: metrics_identity.txt in cwd)
static const char* nfu_metrics_body(void) {
    static char s_body[16384];
    static int s_loaded = 0;
    if (!s_loaded) {
        s_loaded = 1;
        // Live mode: the page asks the real server for its identity and must
        // receive it verbatim, so the captured blob is not offered at all.
        if (getenv("NEFU_MCOOK_LIVE")) return NULL;
        const char* path = getenv("NEFU_METRICS_FILE");
        if (!path || !*path) path = "metrics_identity.txt";
        FILE* f = fopen(path, "rb");
        if (f) {
            size_t n = fread(s_body, 1, sizeof(s_body) - 1, f);
            fclose(f);
            while (n > 0 && (s_body[n - 1] == '\n' || s_body[n - 1] == '\r' || s_body[n - 1] == ' ')) n--;
            s_body[n] = 0;
            if (!s_body[0]) return NULL;
            fprintf(stderr, "\n[MID-LOAD] %s chars=%u\n", path, (unsigned)n);
            fflush(stderr);
        }
    }
    return s_body[0] ? s_body : NULL;
}

static JSValue nfu_metricsBody(JSContext* c, JSValueConst thisv, int argc,
                               JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    const char* s = nfu_metrics_body();
    return s ? JS_NewString(c, s) : JS_NewString(c, "");
}

// Metric session proof. Every social request carries a 3PSIDMCSP string derived
// from the analytics cookie; the server compares it against its own derivation
// and answers 420 to any mismatch, which the client reports as a client version
// mismatch and never recovers from. The engine's derivation of that token comes
// out one bit different from a real browser's, so the exact token observed on a
// successful request is served from disk instead:
//   NEFU_MCSP_FILE=<path>   (default: mcsp.txt in cwd)
static const char* nfu_mcsp(void) {
    static char s_sp[256];
    static int s_loaded = 0;
    if (!s_loaded) {
        s_loaded = 1;
        const char* path = getenv("NEFU_MCSP_FILE");
        if (!path || !*path) path = "mcsp.txt";
        FILE* f = fopen(path, "rb");
        if (f) {
            size_t n = fread(s_sp, 1, sizeof(s_sp) - 1, f);
            fclose(f);
            while (n > 0 && (s_sp[n - 1] == '\n' || s_sp[n - 1] == '\r' || s_sp[n - 1] == ' ')) n--;
            s_sp[n] = 0;
            if (!s_sp[0]) return NULL;
            fprintf(stderr, "\n[MCSP-LOAD] %s chars=%u\n", path, (unsigned)n);
            fflush(stderr);
        }
    }
    return s_sp[0] ? s_sp : NULL;
}

// Live analytics bundle for the room join. The game-server options carry a
// generalCookies object minted for the current visit; replaying a stale one is
// rejected with code 4045 before the handshake, so the accepted bundle is served
// from disk:
//   NEFU_GENCOOK_FILE=<path>   (default: gencook.json in cwd)
static const char* nfu_gencook(void) {
    static char* s_gc = NULL;
    static int s_loaded = 0;
    if (!s_loaded) {
        s_loaded = 1;
        const char* path = getenv("NEFU_GENCOOK_FILE");
        if (!path || !*path) path = "gencook.json";
        FILE* f = fopen(path, "rb");
        if (!f) return NULL;
        long sz = 0;
        if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) sz = 0;
        if (sz > 0 && sz < 8192) {
            s_gc = (char*)malloc((size_t)sz + 1);
            if (s_gc) {
                size_t n = fread(s_gc, 1, (size_t)sz, f);
                s_gc[n] = 0;
                fprintf(stderr, "\n[GENCOOK-LOAD] %s chars=%u\n", path, (unsigned)n);
                fflush(stderr);
            }
        }
        fclose(f);
    }
    return (s_gc && s_gc[0]) ? s_gc : NULL;
}

static JSValue nfu_gencookValue(JSContext* c, JSValueConst thisv, int argc,
                                JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    const char* s = nfu_gencook();
    return s ? JS_NewString(c, s) : JS_NewString(c, "");
}

static JSValue nfu_mcspValue(JSContext* c, JSValueConst thisv, int argc,
                             JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    const char* s = nfu_mcsp();
    return s ? JS_NewString(c, s) : JS_NewString(c, "");
}

// The metric identity object every Colyseus join body carries:
//   {"3PAPISID":"N/A",...,"3PSIDMC":<jar cookie>,"3PSIDMCPP":<identity blob>,
//    "3PSIDMCSP":<session proof>}
// The page fills it from localStorage, which is empty in this engine, so without
// this the room server rejects every join with (4215) onAuth failed. Assembled
// from the three captured pieces so all fields belong to one session.
// Build the join-body identity from the jar cookie, an identity blob and the
// session proof. `blob` may be NULL, in which case the captured file is used.
// Live mode (NEFU_MCOOK_LIVE=1) passes the blob the real server just minted,
// because 3PSIDMCPP is a short-lived ticket: replaying a captured one only
// yields (4215) onAuth failed on every join.
// Pull "\"key\":\"value\"" out of a JSON-ish blob. The captured bundle is not
// strictly parsed anywhere else, so this is the only place that needs it.
static int nfu_json_str(const char* src, const char* key, char* out, size_t cap) {
    if (!src || !key || !cap) return 0;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(src, pat);
    if (!p) return 0;
    p += strlen(pat);
    size_t i = 0;
    while (p[i] && p[i] != '"' && i < cap - 1) { out[i] = p[i]; i++; }
    out[i] = 0;
    return i > 0;
}

static const char* nfu_build_mcook(const char* blob, char* out, size_t cap) {
    const char* jar = nfu_cookie_seed();
    const char* sp = nfu_mcsp();
    char mc[3072] = {0}, pp[16384] = {0};
    char spbuf[1024] = {0};
    // The three fields must belong to one session. The captured generalCookies
    // bundle carries a consistent triple, so prefer it wholesale; the jar cookie
    // and the live wire blob are only fallbacks. A missing 3PSIDMCSP used to
    // make the whole identity object NULL, and the room server answered that by
    // kicking the socket (code 1006) immediately after onJoin.
    const char* gc = nfu_gencook();
    if (gc) {
        nfu_json_str(gc, "3PSIDMC", mc, sizeof(mc));
        nfu_json_str(gc, "3PSIDMCPP", pp, sizeof(pp));
        if (!sp || !sp[0]) {
            if (nfu_json_str(gc, "3PSIDMCSP", spbuf, sizeof(spbuf))) sp = spbuf;
        }
    }
    if (jar) {
        const char* p = strstr(jar, "___Secure-3PSIDMC=");
        if (!p) p = strstr(jar, "3PSIDMC=");
        if (p) {
            p = strchr(p, '='); p++;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i < sizeof(mc) - 1) { mc[i] = p[i]; i++; }
            mc[i] = 0;
        }
    }
    if (!mc[0] && jar) {
        const char* p = strstr(jar, "___Secure-3PSIDMC=");
        if (!p) p = strstr(jar, "3PSIDMC=");
        if (p) {
            p = strchr(p, '='); p++;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i < sizeof(mc) - 1) { mc[i] = p[i]; i++; }
            mc[i] = 0;
        }
    }
    if (!pp[0]) {
        if (!blob) blob = nfu_metrics_body();
        if (blob) nfu_json_str(blob, "3PSIDMCPP", pp, sizeof(pp));
    }
    if ((!sp || !sp[0]) && blob) {
        char sb[1024];
        if (nfu_json_str(blob, "3PSIDMCSP", sb, sizeof(sb))) { strcpy(spbuf, sb); sp = spbuf; }
    }
    if (!mc[0] || !pp[0] || !sp) {
        fprintf(stderr, "\n[MCOOK-BUILD] incomplete mc=%u pp=%u sp=%u\n",
                (unsigned)strlen(mc), (unsigned)strlen(pp), sp ? 1u : 0u);
        fflush(stderr);
        return NULL;
    }
    snprintf(out, cap,
             "{\"3PAPISID\":\"N/A\",\"1PAPISID\":\"N/A\",\"3PSID\":\"N/A\","
             "\"1PSID\":\"N/A\",\"3PSIDMC\":\"%s\",\"3PSIDMCPP\":\"%s\","
             "\"3PSIDMCSP\":\"%s\"}", mc, pp, sp);
    fprintf(stderr, "\n[MCOOK-BUILD] chars=%u mc=%u pp=%u%s\n",
            (unsigned)strlen(out), (unsigned)strlen(mc), (unsigned)strlen(pp),
            blob && blob[0] == '{' ? " live" : "");
    fflush(stderr);
    return out;
}

static const char* nfu_metrics_cookies(void) {
    static char s_out[32768];
    return nfu_build_mcook(NULL, s_out, sizeof(s_out));
}

// With an argument the blob is taken from JS (the live /index/metrics/cookies
// answer), so each join carries a freshly minted ticket instead of a replay.
static JSValue nfu_metricsCookiesValue(JSContext* c, JSValueConst thisv, int argc,
                                       JSValueConst* argv) {
    (void)thisv;
    static char s_out[32768];
    const char* blob = NULL;
    if (argc > 0 && JS_IsString(argv[0])) {
        blob = JS_ToCString(c, argv[0]);
    }
    const char* s = nfu_build_mcook(blob, s_out, sizeof(s_out));
    if (blob) JS_FreeCString(c, blob);
    return s ? JS_NewString(c, s) : JS_NewString(c, "");
}

// Local asset cache. bloxd fetches its Rust wasm module through the same JS
// fetch path as everything else and that call is synchronous inside the
// engine's tick; when the CDN answers slowly the tick holds the join chain for
// ~20 s and the world's Colyseus seat reservation ("seat reservation expired")
// dies before the socket is even opened. The module is byte-for-byte constant
// per build, so it can be served from disk:
//   NEFU_WASM_FILE=<path>   (default: bloxd_wasm_bg.wasm in cwd)
static JSValue nfu_localWasm(JSContext* c, JSValueConst thisv, int argc,
                             JSValueConst* argv) {
    (void)thisv; (void)argc; (void)argv;
    const char* path = getenv("NEFU_WASM_FILE");
    if (!path || !*path) path = "bloxd_wasm_bg.wasm";
    return JS_NewString(c, path);
}

static JSValue nfu_readFile(JSContext* c, JSValueConst thisv, int argc,
                            JSValueConst* argv) {
    (void)thisv;
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NULL;
    const char* path = JS_ToCString(c, argv[0]);
    if (!path || !*path) { JS_FreeCString(c, path); return JS_NULL; }
    FILE* f = fopen(path, "rb");
    if (!f) { JS_FreeCString(c, path); return JS_NULL; }
    long sz = 0;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)(64 << 20)) {
        fclose(f);
        JS_FreeCString(c, path);
        return JS_NULL;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); JS_FreeCString(c, path); return JS_NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    JSValue v = JS_NewArrayBufferCopy(c, buf, got);
    free(buf);
    fprintf(stderr, "\n[WASM-DISK] %s bytes=%u\n", path, (unsigned)got);
    fflush(stderr);
    JS_FreeCString(c, path);
    return v;
}

// Full HTTP request (method + headers + body) exposed to the XHR/fetch
// shim. The base __nfuHttpGet above is GET-only; bloxd's anonymous auth and
// Colyseus matchmaking are POST calls that must carry a body, so without
// this binding they silently degrade to GET and the server rejects them
// ("Can't connect to Bloxd"). Returns {status, body, hdr, bodyAB, setCookie}
// so the shim can expose a faithful XHR/fetch surface. Backed by
// platform_http_request which exists on both the Win32 host (WinINet) and the
// bare kernel, so this stays NEFU_BARE-clean.
static JSValue nfu_httpRequest(JSContext* c, JSValueConst thisv, int argc,
                               JSValueConst* argv) {
    (void)thisv;
    JSValue obj = JS_NewObject(c);
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsString(argv[1])) {
        JS_SetPropertyStr(c, obj, "status", JS_NewInt32(c, 0));
        JS_SetPropertyStr(c, obj, "body", JS_NewString(c, ""));
        JS_SetPropertyStr(c, obj, "hdr", JS_NewString(c, ""));
        return obj;
    }
    const char* method = JS_ToCString(c, argv[0]);
    const char* url = JS_ToCString(c, argv[1]);
    char alias[1024];
    nfu_alias_asset_url(url, alias, sizeof(alias));
    char* body_copy = 0; uint32_t body_len = 0;
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        if (JS_IsString(argv[2])) {
            size_t L = 0; const char* s = JS_ToCStringLen(c, &L, argv[2]);
            if (s) { body_copy = (char*)malloc(L ? L : 1);
                     if (body_copy) { memcpy(body_copy, s, L); body_len = (uint32_t)L; }
                     JS_FreeCString(c, s); }
        } else {
            size_t blen = 0; uint8_t* p = JS_GetArrayBuffer(c, &blen, argv[2]);
            if (p) { body_copy = (char*)malloc(blen ? blen : 1);
                     if (body_copy) { memcpy(body_copy, p, blen); body_len = (uint32_t)blen; } }
        }
    }
    const char* headers = 0;
    if (argc >= 4 && JS_IsString(argv[3])) headers = JS_ToCString(c, argv[3]);
    // Traffic-code unblock (see nfu_traffic_code): answer the sign-off POST
    // locally instead of letting it reach the origin and 401.
    if (url && method && strcmp(method, "POST") == 0 && strstr(url, "/index/traffic-code")) {
        const char* tc = nfu_traffic_code();
        if (tc) {
            // The capture writes the whole response body, which is already the
            // JSON envelope the client parses. Wrapping it again produced
            // "{"trafficCode":"{"trafficCode":"..."}" and the client's JSON
            // parse failed, so the traffic check then tripped on the game
            // server ("Traffic checks failed!").
            int already_json = (tc[0] == '{');
            size_t cap = strlen(tc) + 32;
            char* js = (char*)malloc(cap);
            if (js) {
                if (already_json) snprintf(js, cap, "%s", tc);
                else snprintf(js, cap, "{\"trafficCode\":\"%s\"}", tc);
                JS_SetPropertyStr(c, obj, "status", JS_NewInt32(c, 200));
                JS_SetPropertyStr(c, obj, "body", JS_NewString(c, js));
                JS_SetPropertyStr(c, obj, "hdr", JS_NewString(c, "Content-Type: application/json"));
                JS_SetPropertyStr(c, obj, "setCookie", JS_NewString(c, ""));
                free(js);
                fprintf(stderr, "\n[TC-SYNTH] served %u chars\n", (unsigned)strlen(tc));
                fflush(stderr);
                if (body_copy) free(body_copy);
                if (method) JS_FreeCString(c, method);
                if (url) JS_FreeCString(c, url);
                if (headers) JS_FreeCString(c, headers);
                return obj;
            }
        }
    }
    int status = 0; uint8_t* out = 0; uint32_t out_len = 0;
    bool ok = platform_http_request(method, alias, headers,
                                    (const uint8_t*)body_copy, body_len,
                                    &status, &out, &out_len);
    // Response headers travel with the body: Headers.get() and
    // getAllResponseHeaders() have to report what the server really sent
    // (content-type, etag, location, ...), otherwise every page that sniffs a
    // type before parsing sees a blank answer. Collected even when the call
    // "failed": a 404/401 still answers with a header block.
    const char* hdrs = platform_http_last_headers();
    JS_SetPropertyStr(c, obj, "hdr", JS_NewString(c, hdrs ? hdrs : ""));
    if (ok && out && out_len > 0) {
        JS_SetPropertyStr(c, obj, "status", JS_NewInt32(c, status));
        if (nfu_body_is_binary(hdrs) && !nfu_body_looks_text(out, out_len) && out_len <= 64u * 1024u * 1024u) {
            // Binary payload: never decode as UTF-8 (lossy + 2x memory).
            JS_SetPropertyStr(c, obj, "body", JS_NewString(c, ""));
            JS_SetPropertyStr(c, obj, "bodyAB",
                JS_NewArrayBufferCopy(c, out, (size_t)out_len));
        } else {
            JS_SetPropertyStr(c, obj, "body", JS_NewStringLen(c, (const char*)out, out_len));
        }
        kfree(out);
    } else {
        // A 2xx with a zero-length body is not a usable answer: bloxd posts to
        // /images/<Block>.png and hands the reply straight to JSON.parse, so an
        // empty 200 turns into "Unexpected end of JSON input" and aborts the whole
        // world setup. Report it as a failed request instead - the caller then
        // retries, which is what a stock browser does when the body cannot be read.
        JS_SetPropertyStr(c, obj, "status", JS_NewInt32(c, (ok && out_len == 0 && status >= 200 && status < 300) ? 0 : (ok ? status : 0)));
        JS_SetPropertyStr(c, obj, "body", JS_NewString(c, ""));
        if (ok && out_len == 0 && status >= 200 && status < 300) {
            fprintf(stderr, "\n[EMPTY200] %s st=%d hdr=%s\n", url ? url : "?", status, hdrs ? hdrs : "");
            fflush(stderr);
        }
        if (out) kfree(out);
    }
    if (body_copy) free(body_copy);
    // Cookies set by this response, handed over in the same object so the JS
    // document jar can adopt them atomically (the native pointer is only valid
    // until the next request, so it must be copied now).
    {
        const char* sc = platform_http_last_set_cookie();
        JS_SetPropertyStr(c, obj, "setCookie", JS_NewString(c, sc ? sc : ""));
    }
    if (method) JS_FreeCString(c, method);
    if (url) JS_FreeCString(c, url);
    if (headers) JS_FreeCString(c, headers);
    return obj;
}

// ---- dynamic import (ES module) support --------------------------------
// Web bundles (bloxd.io) pull code chunks via import(); quickjs only wires
// that through a runtime module loader. Relative specifiers resolve against
// the URL of the script currently running (mini_js_set_module_base), and the
// bytes are fetched with platform_http_get — the same code path the XHR shim
// uses — which exists on both the Win32 host and the bare kernel, so this
// stays NEFU_BARE-clean.
// ---- guarded allocator (NF_JS_GUARD=1) --------------------------------
// quickjs-heap overflow detector: every allocation gets an 8-byte 0xCC tail
// guard; free/realloc verify it and report the exact pointer+size. Turned on
// for the put_lvalue corruption hunt, off by default (perf).
static void* nfu_gd_alloc(JSMallocState* s, size_t size) {
    if (s->malloc_limit && s->malloc_size + size > s->malloc_limit) return NULL;
    uint8_t* b = (uint8_t*)malloc(size + 24);
    if (!b) return NULL;
    *(size_t*)b = size;
    memset(b + 8 + size, 0xCC, 8);
    s->malloc_count++;
    s->malloc_size += size + 24;
    return b + 8;
}
static void nfu_gd_free(JSMallocState* s, void* p) {
    if (!p) return;
    uint8_t* b = (uint8_t*)p - 8;
    size_t sz = *(size_t*)b;
    const uint8_t* tail = b + 8 + sz;
    if (tail[0] != 0xCC || tail[1] != 0xCC || tail[2] != 0xCC || tail[3] != 0xCC ||
        tail[4] != 0xCC || tail[5] != 0xCC || tail[6] != 0xCC || tail[7] != 0xCC) {
        fprintf(stderr, "js: HEAP GUARD OVERFLOW ptr=%p size=%zu\n", p, sz);
        fflush(stderr);
    }
    s->malloc_count--;
    s->malloc_size -= sz + 24;
    free(b);
}
static void* nfu_gd_realloc(JSMallocState* s, void* p, size_t size) {
    if (!p) return nfu_gd_alloc(s, size);
    if (size == 0) { nfu_gd_free(s, p); return NULL; }
    uint8_t* b = (uint8_t*)p - 8;
    size_t osz = *(size_t*)b;
    const uint8_t* tail = b + 8 + osz;
    for (int i = 0; i < 8; i++) {
        if (tail[i] != 0xCC) {
            fprintf(stderr, "js: HEAP GUARD OVERFLOW(realloc) ptr=%p size=%zu\n", p, osz);
            fflush(stderr);
            break;
        }
    }
    if (s->malloc_limit && s->malloc_size - osz + size > s->malloc_limit) return NULL;
    uint8_t* nb = (uint8_t*)realloc(b, size + 24);
    if (!nb) return NULL;
    *(size_t*)nb = size;
    memset(nb + 8 + size, 0xCC, 8);
    s->malloc_size += (long)(size + 24) - (long)(osz + 24);
    return nb + 8;
}
static size_t nfu_gd_usable(const void* p) {
    if (!p) return 0;
    return *(const size_t*)((const uint8_t*)p - 8);
}
static JSMallocFunctions nfu_gd_mf = { nfu_gd_alloc, nfu_gd_free, nfu_gd_realloc, nfu_gd_usable };

static char s_mod_base[768];   // absolute URL of the currently running script

static void nfu_mod_set_base(const char* url) {
    if (!url) { s_mod_base[0] = 0; return; }
    snprintf(s_mod_base, sizeof(s_mod_base), "%s", url);
}

static char* nfu_mod_normalize(JSContext* c, const char* base_cname,
                               const char* name, void* opaque) {
    (void)opaque;
    if (!name || !name[0]) { JS_ThrowTypeError(c, "empty module name"); return NULL; }
    if (strstr(name, "://")) return js_strdup(c, name);
    const char* base = (base_cname && strstr(base_cname, "://")) ? base_cname : s_mod_base;
    if (!base || !strstr(base, "://")) {
        JS_ThrowReferenceError(c, "could not resolve module '%s' (no base URL)", name);
        return NULL;
    }
    char buf[768];
    size_t bl = strlen(base);
    if (bl >= sizeof(buf)) bl = sizeof(buf) - 1;
    memcpy(buf, base, bl); buf[bl] = 0;
    char* q = strchr(buf, '?'); if (q) *q = 0;            // drop query on base
    char* path0 = strchr(strstr(buf, "://") + 3, '/');
    if (!path0) {                                          // bare origin
        path0 = buf + strlen(buf);
        *path0++ = '/';
        *path0 = 0;
        path0 = NULL;
    }
    char* last = path0 ? strrchr(path0, '/') : NULL;
    if (last) *last = 0;                                   // strip filename
    const char* segs[96]; int ns = 0;
    if (path0) {
        char* p2 = path0 + 1;
        while (*p2 && ns < 96) {
            segs[ns++] = p2;
            char* sl = strchr(p2, '/');
            if (!sl) break;
            *sl = 0;
            p2 = sl + 1;
        }
    }
    char nbuf[512];
    snprintf(nbuf, sizeof(nbuf), "%s", name);
    char* toks[64]; int nt = 0;
    char* t = nbuf;
    while (*t && nt < 64) {
        toks[nt++] = t;
        char* sl = strchr(t, '/');
        if (!sl) break;
        *sl = 0;
        t = sl + 1;
    }
    for (int i = 0; i < nt; i++) {
        if (!strcmp(toks[i], ".")) continue;
        if (!strcmp(toks[i], "")) { ns = 0; continue; }    // root reset
        if (!strcmp(toks[i], "..")) { if (ns > 0) ns--; continue; }
        if (ns < 96) segs[ns++] = toks[i];
    }
    char out[900];
    size_t hostlen = path0 ? (size_t)(path0 - buf) : strlen(buf);
    if (hostlen >= sizeof(out)) { JS_ThrowTypeError(c, "url too long"); return NULL; }
    memcpy(out, buf, hostlen);
    int o = (int)hostlen;
    for (int i = 0; i < ns; i++) {
        int sl = (int)strlen(segs[i]);
        if (o + 1 + sl + 1 >= (int)sizeof(out)) { JS_ThrowTypeError(c, "url too long"); return NULL; }
        out[o++] = '/';
        memcpy(out + o, segs[i], sl);
        o += sl;
    }
    out[o] = 0;
    return js_strdup(c, out);
}

// Restore the caller's interrupt budget after a chunk load, but never hand
// back an already-expired deadline. quickjs evaluates the module body in the
// SAME pending job right after this loader returns (JS_LoadModuleInternal ->
// js_host_resolve_imported_module -> our loader, then JS_EvalFunction). If the
// caller was mini_js_tick (250 ms frame budget) and the download+compile took
// seconds, restoring that stale deadline makes JS_EvalFunction throw
// "interrupted" immediately: the chunk loads, "module loaded" is logged, but
// the engine never actually starts. Keep a floor so module evaluation can
// finish; the floor is per-call (RunCtx is stack-local per run/tick), so it
// cannot silently extend later frames.
static void nfu_restore_deadline(RunCtx* rc, uint32_t saved_deadline) {
    if (!rc) return;
    uint32_t floor = platform_tick_ms() + 30000;   // module evaluation budget
    rc->deadline = saved_deadline < floor ? floor : saved_deadline;
}

static JSModuleDef* nfu_mod_loader(JSContext* c, const char* name, void* opaque) {
    (void)opaque;
    if (!name || !strstr(name, "://")) {
        JS_ThrowReferenceError(c, "could not load module '%s'", name ? name : "(null)");
        return NULL;
    }
    // Loading a chunk means downloading it AND compiling it, and the engine
    // chunk is several megabytes - on the bare kernel that takes far longer
    // than the per-frame budget the caller has. The interrupt handler stops
    // the parse when that budget runs out, and quickjs then reports the
    // truncated source as a random SyntaxError ("expecting ';'"), which looks
    // like a broken download but is not: the bytes are fine.
    //
    // So the module load gets its own, generous budget. The caller's deadline
    // is restored afterwards (with a floor, see nfu_restore_deadline) so the
    // frame budget is not silently extended yet module evaluation survives.
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    uint32_t saved_deadline = rc ? rc->deadline : 0;
    if (rc) rc->deadline = platform_tick_ms() + 120000;   // 2 min for one chunk
    uint8_t* body = 0; uint32_t blen = 0;
    bool ok = platform_http_get(name, &body, &blen);
    if (!ok || !body || blen == 0) {
        if (body) kfree(body);
        nfu_restore_deadline(rc, saved_deadline);
        JS_ThrowReferenceError(c, "could not load module '%s'", name);
        return NULL;
    }
#ifndef NEFU_BARE
    // dump module sources: a parser crash (put_lvalue) during compile can
    // then be reproduced locally with qjs_run on the exact bytes
    {
        static unsigned mod_dump_n = 0;
        const char* ddir = getenv("NF_DUMP_JS");
        if (ddir && *ddir) {
            char p[512];
            snprintf(p, sizeof(p), "%s/mod_%u.js", ddir, ++mod_dump_n);
            FILE* df = fopen(p, "wb");
            if (df) { fwrite(body, 1, blen, df); fclose(df); }
        }
    }
#endif
    // NUL-terminate a private copy before parsing.  QuickJS's lexer reads one
    // byte past input_len in several token paths (quickjs.c: "c = *p++;" is
    // only then checked against buf_end, and the escape path tests
    // "p > s->buf_end"), so body[blen] acts as an EOF sentinel.  HbBuf leaves
    // that spare byte uninitialised, and a random sentinel produced a RANDOM
    // SyntaxError on perfectly valid bytes: verified on the metal with FNV-1a
    // (hb: bodyfnv == moddiag fnv == the offline file, while JS_Eval and a
    // second compile of the same buffer both failed).  browser.cpp already
    // NUL-terminates the page bundle for exactly this reason; the module path
    // was missing it.  A copy is used (not body[blen] = 0) so no call site of
    // platform_http_get has to rely on spare capacity.
    char* mod_src = (char*)kalloc((size_t)blen + 1);
    if (!mod_src) {
        kfree(body);
        nfu_restore_deadline(rc, saved_deadline);
        JS_ThrowReferenceError(c, "out of memory loading '%s'", name);
        return NULL;
    }
    memcpy(mod_src, body, blen);
    mod_src[blen] = 0;   // EOF sentinel for the lexer
    kfree(body);
    body = (uint8_t*)mod_src;
    // Parse with an independent deadline.  The surrounding tick's 250 ms
    // frame budget (or a deadline restored by another loader) must never
    // interrupt the parser: doing so yields a random SyntaxError and the
    // chunk is silently dropped until bloxd retries it.
    uint32_t saved_mod_dl = g_mod_parse_deadline;
    g_mod_parse_deadline = platform_tick_ms() + kModParseBudgetMs;
    g_mod_parse_depth++;
    JSValue fv = JS_Eval(c, (const char*)body, blen, name,
                         JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    g_mod_parse_depth--;
    g_mod_parse_deadline = saved_mod_dl;
    if (JS_IsException(fv)) {
        // A chunk that fails to compile leaves the engine half-loaded: the
        // page keeps running but the game never starts. Say so explicitly,
        // and show what the body looked like - a truncated download and a
        // genuine syntax error need very different fixes.
        JSValue ex = JS_GetException(c);
        const char* em = JS_ToCString(c, ex);
        fprintf(stderr, "browser: module compile FAILED %s: %s\n",
                name ? name : "?", em ? em : "?");
        // Build both samples first, then emit with a single fprintf.  The bare
        // kernel's fputc() writes to COM1 while fprintf(stderr) writes to the
        // debug console (port 0xE9), so the old byte-at-a-time loop made the
        // head/tail samples invisible in the capture that holds the error line.
        char head[65], tail[65];
        uint32_t hn = blen < 64 ? blen : 64;
        for (uint32_t d = 0; d < hn; d++) {
            unsigned char ch = body[d];
            head[d] = (ch >= 32 && ch < 127) ? (char)ch : '.';
        }
        head[hn] = 0;
        uint32_t ts = blen > 64 ? blen - 64 : 0;
        for (uint32_t d = ts; d < blen; d++) {
            unsigned char ch = body[d];
            tail[d - ts] = (ch >= 32 && ch < 127) ? (char)ch : '.';
        }
        tail[blen - ts] = 0;
        fprintf(stderr, "  body %u bytes, head: %s\n  tail: %s\n",
                (unsigned)blen, head, tail);
        fflush(stderr);

        // ---- byte-level diagnosis for the intermittent bogus SyntaxError ----
        // The head/tail are clean while the parser fails on a random token in
        // the middle. A checksum lets the next run compare these exact bytes
        // against the HTTP layer's own checksum (hb: bodyfnv), and a full hex
        // dump lets us diff them against the real chunk offline. Recompiling
        // the SAME buffer once separates "bytes are corrupt" from "transient
        // engine state"; a success is adopted so the chunk is not lost.
        {
            uint32_t hv = 2166136261u;
            for (uint32_t i2 = 0; i2 < blen; i2++) { hv ^= body[i2]; hv *= 16777619u; }
            fprintf(stderr, "browser: moddiag %s len=%u fnv=%08x\n",
                    name ? name : "?", (unsigned)blen, (unsigned)hv);
            if (blen <= 4096) {
                for (uint32_t off = 0; off < blen; off += 32) {
                    char ln[160]; int k = 0;
                    k += snprintf(ln + k, sizeof(ln) - k, "%04x ", (unsigned)off);
                    for (uint32_t j = 0; j < 32 && off + j < blen; j++) {
                        if (k + 3 < (int)sizeof(ln))
                            k += snprintf(ln + k, sizeof(ln) - k, "%02x", body[off + j]);
                    }
                    ln[k] = 0;
                    fprintf(stderr, "  md %s\n", ln);
                }
            }
            fflush(stderr);
        }
        if (em) { JS_FreeCString(c, em); em = 0; }

        uint32_t saved_mod_dl2 = g_mod_parse_deadline;
        g_mod_parse_deadline = platform_tick_ms() + kModParseBudgetMs;
        g_mod_parse_depth++;
        JSValue fv2 = JS_Eval(c, (const char*)body, blen, name,
                              JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        g_mod_parse_depth--;
        g_mod_parse_deadline = saved_mod_dl2;
        if (!JS_IsException(fv2)) {
            fprintf(stderr, "browser: moddiag retry-compile OK %s -- adopting\n",
                    name ? name : "?");
            fflush(stderr);
            JS_FreeValue(c, ex);
            kfree(body);
            nfu_restore_deadline(rc, saved_deadline);
            JSModuleDef* m2 = (JSModuleDef*)JS_VALUE_GET_PTR(fv2);
            JSValue meta2 = JS_GetImportMeta(c, m2);
            if (!JS_IsException(meta2)) {
                JS_DefinePropertyValueStr(c, meta2, "url", JS_NewString(c, name), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(c, meta2, "main", JS_FALSE, JS_PROP_C_W_E);
                JS_FreeValue(c, meta2);
            } else {
                JSValue e2 = JS_GetException(c); JS_FreeValue(c, e2);
            }
            JS_FreeValue(c, fv2);
            return m2;
        }
        // Identical bytes still fail: discard the second exception and restore
        // the first so the loader contract (NULL + pending exception) holds.
        JS_FreeValue(c, fv2);
        {
            JSValue e2 = JS_GetException(c); JS_FreeValue(c, e2);
        }
        fprintf(stderr, "browser: moddiag retry-compile FAILED %s -- bytes bad\n",
                name ? name : "?");
        fflush(stderr);
        JS_Throw(c, ex);          // ownership of ex moves to the context
        kfree(body);
        nfu_restore_deadline(rc, saved_deadline);
        return NULL;
    }
    fprintf(stderr, "browser: module loaded %s (%u bytes)\n",
            name ? name : "?", (unsigned)blen);
    fflush(stderr);
    kfree(body);
    nfu_restore_deadline(rc, saved_deadline);
    JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(fv);   // tag == JS_TAG_MODULE
    JSValue meta = JS_GetImportMeta(c, m);
    if (!JS_IsException(meta)) {
        JS_DefinePropertyValueStr(c, meta, "url", JS_NewString(c, name), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(c, meta, "main", JS_FALSE, JS_PROP_C_W_E);
        JS_FreeValue(c, meta);
    } else {
        JS_GetException(c);                                // meta is optional
    }
    JS_FreeValue(c, fv);   /* the evaluator already holds its own ref */
    return m;
}

// ---- WebSocket bridge -------------------------------------------------
// The shim owns the HTML-facing WebSocket class; the native surface is just
// open / state / send / poll / close. poll() returns:
//   undefined   -> nothing queued yet
//   null        -> the socket closed and its queue is drained
//   string      -> text message (already decoded from UTF-8)
//   ArrayBuffer -> binary message
// Empty frames are dropped (a zero-length message is indistinguishable from
// "nothing queued" in this contract, and no game protocol needs them).
static JSValue nfu_wsOpen(JSContext* c, JSValueConst thisv, int argc,
                          JSValueConst* argv) {
    (void)thisv;
    if (argc < 1 || !JS_IsString(argv[0])) return JS_NewInt32(c, -1);
    const char* u = JS_ToCString(c, argv[0]);
    const char* xh = (argc >= 2 && JS_IsString(argv[1])) ? JS_ToCString(c, argv[1]) : 0;
    int h = u ? platform_ws_open_hdr(u, xh) : -1;
    if (xh) JS_FreeCString(c, xh);
    if (u) JS_FreeCString(c, u);
    return JS_NewInt32(c, h);
}

static JSValue nfu_wsState(JSContext* c, JSValueConst thisv, int argc,
                           JSValueConst* argv) {
    (void)thisv;
    int32_t h = -1;
    if (argc >= 1) JS_ToInt32(c, &h, argv[0]);
    return JS_NewInt32(c, platform_ws_state(h));
}

static JSValue nfu_wsSend(JSContext* c, JSValueConst thisv, int argc,
                          JSValueConst* argv) {
    (void)thisv;
    if (argc < 2) return JS_NewInt32(c, -1);
    int32_t h = -1;
    JS_ToInt32(c, &h, argv[0]);
    if (JS_IsString(argv[1])) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(c, &len, argv[1]);   // UTF-8 bytes
        int rc = s ? platform_ws_send(h, (const uint8_t*)s, (int)len, 0) : -1;
        if (s) JS_FreeCString(c, s);
        return JS_NewInt32(c, rc < 0 ? -1 : (int)len);
    }
    size_t blen = 0;
    uint8_t* p = JS_GetArrayBuffer(c, &blen, argv[1]);
    if (!p) return JS_NewInt32(c, -1);
    int rc = platform_ws_send(h, p, (int)blen, 1);
    return JS_NewInt32(c, rc < 0 ? -1 : (int)blen);
}

static JSValue nfu_wsPoll(JSContext* c, JSValueConst thisv, int argc,
                          JSValueConst* argv) {
    (void)thisv;
    if (argc < 1) return JS_UNDEFINED;
    int32_t h = -1;
    JS_ToInt32(c, &h, argv[0]);
    int bin = 0;
    int n = platform_ws_peek(h, &bin);
    if (n < 0) return JS_NULL;
    if (n == 0) return JS_UNDEFINED;
    if (n > (1 << 21)) {                      // >2 MiB: reject and drop
        uint8_t tmp[512];
        int left = n;
        while (left > 0) {
            int take = left > (int)sizeof(tmp) ? (int)sizeof(tmp) : left;
            if (platform_ws_read(h, tmp, take) <= 0) break;
            left -= take;
        }
        return JS_UNDEFINED;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)n);
    if (!buf) return JS_UNDEFINED;
    int got = 0;
    while (got < n) {
        int r = platform_ws_read(h, buf + got, n - got);
        if (r <= 0) break;
        got += r;
    }
    JSValue v;
    if (bin) {
        v = JS_NewArrayBufferCopy(c, buf, (size_t)got);
    } else {
        // text: bytes are valid UTF-8 by protocol
        v = JS_NewStringLen(c, (const char*)buf, (size_t)got);
    }
    free(buf);
    return v;
}

static JSValue nfu_wsClose(JSContext* c, JSValueConst thisv, int argc,
                           JSValueConst* argv) {
    (void)thisv;
    int32_t h = -1;
    if (argc >= 1) JS_ToInt32(c, &h, argv[0]);
    platform_ws_close(h);
    return JS_UNDEFINED;
}

// make a GL object bound to a canvas id (shared by canvas wrapper + DOM node)
static JSValue make_gl_obj(JSContext* c, const char* cid) {
    JSValue g = JS_NewObjectClass(c, s_gl_cls);
    // Remember this context for receivers that arrive untagged (see gl_cid_of).
    snprintf(s_gl_last_cid, sizeof(s_gl_last_cid), "%s", cid);
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
    // Babylon's attribute binder turns attribute arrays off at the end of every
    // mesh; disableAttributeByIndex then threw "not a function (got undefined)"
    // inside the noa render loop. The pointer state is re-established per mesh
    // by glattptr, so disabling can be inert.
    JS_SetPropertyStr(c, g, "disableVertexAttribArray", JS_NewCFunction(c, nfu_gl_delete, "disableVertexAttribArray", 1));
    JS_SetPropertyStr(c, g, "createShader", JS_NewCFunction(c, nfu_createShader, "createShader", 1));
    JS_SetPropertyStr(c, g, "shaderSource", JS_NewCFunction(c, nfu_shaderSource, "shaderSource", 2));
    JS_SetPropertyStr(c, g, "createProgram", JS_NewCFunction(c, nfu_createProgram, "createProgram", 0));
    JS_SetPropertyStr(c, g, "createBuffer", JS_NewCFunction(c, nfu_createBuffer, "createBuffer", 0));
    JS_SetPropertyStr(c, g, "bindBuffer", JS_NewCFunction(c, nfu_bindBuffer, "bindBuffer", 2));
    JS_SetPropertyStr(c, g, "bufferData", JS_NewCFunction(c, nfu_bufferData, "bufferData", 3));
    // WebGL1 bufferSubData: engines stream mesh updates through it every frame.
    // Missing here, three.js' updateDirectly threw "not a function" inside the
    // engine's tick loop, so no draw was ever submitted.
    JS_SetPropertyStr(c, g, "bufferSubData", JS_NewCFunction(c, nfu_bufferSubData, "bufferSubData", 3));
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
    // ---- WebGL1 (GLES 2.0) state setters ----
    // bindVertexArray/createVertexArray are deliberately NOT registered: the
    // gl object must present the WebGL1 surface so engines probing for
    // WebGL2-only entry points ('createVertexArray' in gl) take their
    // WebGL1 code path.
    JS_SetPropertyStr(c, g, "depthMask", JS_NewCFunctionMagic(c, nfu_gl, "depthMask", 1, JS_CFUNC_generic_magic, 30));
    JS_SetPropertyStr(c, g, "colorMask", JS_NewCFunctionMagic(c, nfu_gl, "colorMask", 4, JS_CFUNC_generic_magic, 31));
    JS_SetPropertyStr(c, g, "scissor", JS_NewCFunctionMagic(c, nfu_gl, "scissor", 4, JS_CFUNC_generic_magic, 32));
    JS_SetPropertyStr(c, g, "blendFuncSeparate", JS_NewCFunctionMagic(c, nfu_gl, "blendFuncSeparate", 4, JS_CFUNC_generic_magic, 22));   // first two args = blendFunc
    JS_SetPropertyStr(c, g, "pixelStorei", JS_NewCFunctionMagic(c, nfu_gl, "pixelStorei", 2, JS_CFUNC_generic_magic, 33));
    JS_SetPropertyStr(c, g, "getUniformLocation", JS_NewCFunction(c, nfu_getUniformLocation, "getUniformLocation", 2));
    JS_SetPropertyStr(c, g, "getAttribLocation", JS_NewCFunction(c, nfu_getAttribLocation, "getAttribLocation", 2));
    // array forms of the uniform setters (light/bone packs)
    JS_SetPropertyStr(c, g, "uniform1fv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform1fv", 2, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "uniform2fv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform2fv", 2, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "uniform3fv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform3fv", 2, JS_CFUNC_generic_magic, 3));
    JS_SetPropertyStr(c, g, "uniform4fv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform4fv", 2, JS_CFUNC_generic_magic, 4));
    JS_SetPropertyStr(c, g, "uniform1iv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform1iv", 2, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "uniform2iv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform2iv", 2, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "uniform3iv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform3iv", 2, JS_CFUNC_generic_magic, 3));
    JS_SetPropertyStr(c, g, "uniform4iv", JS_NewCFunctionMagic(c, nfu_uniformv, "uniform4iv", 2, JS_CFUNC_generic_magic, 4));
    // matrix uniforms: the implementation existed but was never exported, so any
    // engine using perspective/model matrices threw "not a function (got undefined)"
    // at its very first frame - which is why 3D scenes could upload textures and
    // still never draw.
    JS_SetPropertyStr(c, g, "uniformMatrix2fv", JS_NewCFunctionMagic(c, nfu_uniformMatrix, "uniformMatrix2fv", 3, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(c, g, "uniformMatrix3fv", JS_NewCFunctionMagic(c, nfu_uniformMatrix, "uniformMatrix3fv", 3, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "uniformMatrix4fv", JS_NewCFunctionMagic(c, nfu_uniformMatrix, "uniformMatrix4fv", 3, JS_CFUNC_generic_magic, 2));
    // dedicated status/count queries (bool vs number, see nfu_getProgramParameter)
    JS_SetPropertyStr(c, g, "getShaderParameter", JS_NewCFunction(c, nfu_getShaderParameter, "getShaderParameter", 2));
    JS_SetPropertyStr(c, g, "getProgramParameter", JS_NewCFunction(c, nfu_getProgramParameter, "getProgramParameter", 2));
    JS_SetPropertyStr(c, g, "getActiveAttrib", JS_NewCFunction(c, nfu_getActive, "getActiveAttrib", 2));
    JS_SetPropertyStr(c, g, "getActiveUniform", JS_NewCFunction(c, nfu_getActive, "getActiveUniform", 2));
    JS_SetPropertyStr(c, g, "getError", JS_NewCFunctionMagic(c, nfu_ret_true, "getError", 0, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c, g, "getShaderInfoLog", JS_NewCFunctionMagic(c, nfu_ret_true, "getShaderInfoLog", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "getProgramInfoLog", JS_NewCFunctionMagic(c, nfu_ret_true, "getProgramInfoLog", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(c, g, "createTexture", JS_NewCFunction(c, nfu_createTexture, "createTexture", 0));
    // ---- capability queries the page asks for before it draws ----
    JS_SetPropertyStr(c, g, "getExtension", JS_NewCFunction(c, nfu_getExtension, "getExtension", 1));
    JS_SetPropertyStr(c, g, "getSupportedExtensions", JS_NewCFunction(c, nfu_getSupportedExtensions, "getSupportedExtensions", 0));
    JS_SetPropertyStr(c, g, "getParameter", JS_NewCFunction(c, nfu_getParameter, "getParameter", 1));
    JS_SetPropertyStr(c, g, "getContextAttributes", JS_NewCFunction(c, nfu_getContextAttributes, "getContextAttributes", 0));
    JS_SetPropertyStr(c, g, "getShaderPrecisionFormat", JS_NewCFunction(c, nfu_getShaderPrecisionFormat, "getShaderPrecisionFormat", 2));
    JS_SetPropertyStr(c, g, "readPixels", JS_NewCFunction(c, nfu_readPixels, "readPixels", 7));
    JS_SetPropertyStr(c, g, "deleteShader", JS_NewCFunction(c, nfu_gl_delete, "deleteShader", 1));
    JS_SetPropertyStr(c, g, "deleteProgram", JS_NewCFunction(c, nfu_gl_delete, "deleteProgram", 1));
    JS_SetPropertyStr(c, g, "deleteBuffer", JS_NewCFunction(c, nfu_gl_delete, "deleteBuffer", 1));
    JS_SetPropertyStr(c, g, "deleteTexture", JS_NewCFunction(c, nfu_gl_delete, "deleteTexture", 1));
    JS_SetPropertyStr(c, g, "deleteVertexArray", JS_NewCFunction(c, nfu_gl_delete, "deleteVertexArray", 1));
    JS_SetPropertyStr(c, g, "deleteFramebuffer", JS_NewCFunction(c, nfu_gl_delete, "deleteFramebuffer", 1));
    JS_SetPropertyStr(c, g, "deleteRenderbuffer", JS_NewCFunction(c, nfu_gl_delete, "deleteRenderbuffer", 1));
    // state calls the WebGL1 engines issue but the rasterizer can treat as
    // no-ops (no stencil buffer, no blend equation, constant attribs, no
    // dynamic texture update yet): present but inert, so nothing throws
    JS_SetPropertyStr(c, g, "frontFace", JS_NewCFunction(c, nfu_gl_delete, "frontFace", 1));
    JS_SetPropertyStr(c, g, "blendEquation", JS_NewCFunction(c, nfu_gl_delete, "blendEquation", 1));
    JS_SetPropertyStr(c, g, "blendEquationSeparate", JS_NewCFunction(c, nfu_gl_delete, "blendEquationSeparate", 2));
    JS_SetPropertyStr(c, g, "blendColor", JS_NewCFunction(c, nfu_gl_delete, "blendColor", 4));
    JS_SetPropertyStr(c, g, "stencilFunc", JS_NewCFunction(c, nfu_gl_delete, "stencilFunc", 3));
    JS_SetPropertyStr(c, g, "stencilFuncSeparate", JS_NewCFunction(c, nfu_gl_delete, "stencilFuncSeparate", 4));
    JS_SetPropertyStr(c, g, "stencilMask", JS_NewCFunction(c, nfu_gl_delete, "stencilMask", 1));
    JS_SetPropertyStr(c, g, "stencilMaskSeparate", JS_NewCFunction(c, nfu_gl_delete, "stencilMaskSeparate", 2));
    JS_SetPropertyStr(c, g, "stencilOp", JS_NewCFunction(c, nfu_gl_delete, "stencilOp", 3));
    JS_SetPropertyStr(c, g, "stencilOpSeparate", JS_NewCFunction(c, nfu_gl_delete, "stencilOpSeparate", 4));
    JS_SetPropertyStr(c, g, "lineWidth", JS_NewCFunction(c, nfu_gl_delete, "lineWidth", 1));
    JS_SetPropertyStr(c, g, "vertexAttrib1f", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib1f", 2));
    JS_SetPropertyStr(c, g, "vertexAttrib2f", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib2f", 3));
    JS_SetPropertyStr(c, g, "vertexAttrib3f", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib3f", 4));
    JS_SetPropertyStr(c, g, "vertexAttrib4f", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib4f", 5));
    JS_SetPropertyStr(c, g, "texSubImage2D", JS_NewCFunctionMagic(c, nfu_gl, "texSubImage2D", 9, JS_CFUNC_generic_magic, 34));
    JS_SetPropertyStr(c, g, "generateMipmap", JS_NewCFunction(c, nfu_gl_delete, "generateMipmap", 1));
    JS_SetPropertyStr(c, g, "flush", JS_NewCFunction(c, nfu_gl_delete, "flush", 0));
    JS_SetPropertyStr(c, g, "finish", JS_NewCFunction(c, nfu_gl_delete, "finish", 0));
    // isContextLost: the context is never lost on the software rasterizer
    JS_SetPropertyStr(c, g, "isContextLost", JS_NewCFunction(c, nfu_ret_false, "isContextLost", 0));
    // Babylon's clear() path calls the WebGL2 clear-value setters before gl.clear.
    // They were absent, so the render loop died every frame with
    // "not a function (got undefined)" at clearDepth and no 3D frame ever
    // reached the rasterizer. swgl clears depth to 1 and stencil to 0, which is
    // exactly the default these setters ask for, so inert stubs are enough.
    JS_SetPropertyStr(c, g, "clearDepth", JS_NewCFunction(c, nfu_gl_delete, "clearDepth", 1));
    JS_SetPropertyStr(c, g, "clearStencil", JS_NewCFunction(c, nfu_gl_delete, "clearStencil", 1));
    JS_SetPropertyStr(c, g, "clearBufferiv", JS_NewCFunction(c, nfu_gl_delete, "clearBufferiv", 3));
    JS_SetPropertyStr(c, g, "clearBufferfv", JS_NewCFunction(c, nfu_gl_delete, "clearBufferfv", 4));
    JS_SetPropertyStr(c, g, "clearBufferuiv", JS_NewCFunction(c, nfu_gl_delete, "clearBufferuiv", 4));
    JS_SetPropertyStr(c, g, "clearBufferfi", JS_NewCFunction(c, nfu_gl_delete, "clearBufferfi", 4));
    JS_SetPropertyStr(c, g, "drawBuffers", JS_NewCFunction(c, nfu_gl_delete, "drawBuffers", 1));
    JS_SetPropertyStr(c, g, "readBuffer", JS_NewCFunction(c, nfu_gl_delete, "readBuffer", 1));
    // ---- remaining WebGL entry points the Babylon depth/stencil state machine
    // and material binder call unconditionally every frame. polygonOffset was
    // the one that killed the noa render loop ("not a function (got undefined)"
    // at DepthCullingState.apply), so the whole set is stubbed here.
    JS_SetPropertyStr(c, g, "polygonOffset", JS_NewCFunction(c, nfu_gl_delete, "polygonOffset", 2));
    JS_SetPropertyStr(c, g, "polygonOffsetFill", JS_NewCFunction(c, nfu_gl_delete, "polygonOffsetFill", 2));
    JS_SetPropertyStr(c, g, "depthRange", JS_NewCFunction(c, nfu_gl_delete, "depthRange", 2));
    JS_SetPropertyStr(c, g, "hint", JS_NewCFunction(c, nfu_gl_delete, "hint", 2));
    JS_SetPropertyStr(c, g, "bindAttribLocation", JS_NewCFunction(c, nfu_gl_delete, "bindAttribLocation", 3));
    JS_SetPropertyStr(c, g, "texParameterf", JS_NewCFunction(c, nfu_gl_delete, "texParameterf", 3));
    JS_SetPropertyStr(c, g, "validateProgram", JS_NewCFunction(c, nfu_gl_delete, "validateProgram", 1));
    JS_SetPropertyStr(c, g, "vertexAttrib1fv", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib1fv", 2));
    JS_SetPropertyStr(c, g, "vertexAttrib2fv", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib2fv", 2));
    JS_SetPropertyStr(c, g, "vertexAttrib3fv", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib3fv", 2));
    JS_SetPropertyStr(c, g, "vertexAttrib4fv", JS_NewCFunction(c, nfu_gl_delete, "vertexAttrib4fv", 2));
    JS_SetPropertyStr(c, g, "vertexAttribPointerOffset", JS_NewCFunction(c, nfu_gl_delete, "vertexAttribPointerOffset", 2));
    // instanced drawing: inert, so babylon's instanced meshes simply do not draw
    // instead of taking the render loop down with a TypeError.
    JS_SetPropertyStr(c, g, "drawArraysInstanced", JS_NewCFunction(c, nfu_gl_delete, "drawArraysInstanced", 4));
    JS_SetPropertyStr(c, g, "drawElementsInstanced", JS_NewCFunction(c, nfu_gl_delete, "drawElementsInstanced", 5));
    JS_SetPropertyStr(c, g, "vertexAttribDivisor", JS_NewCFunction(c, nfu_gl_delete, "vertexAttribDivisor", 2));
    // render targets: the software rasterizer draws straight to the canvas, so
    // framebuffer objects are accepted and reported complete but never bound.
    JS_SetPropertyStr(c, g, "createFramebuffer", JS_NewCFunction(c, nfu_gl_delete, "createFramebuffer", 0));
    JS_SetPropertyStr(c, g, "bindFramebuffer", JS_NewCFunction(c, nfu_gl_delete, "bindFramebuffer", 2));
    JS_SetPropertyStr(c, g, "framebufferTexture2D", JS_NewCFunction(c, nfu_gl_delete, "framebufferTexture2D", 5));
    JS_SetPropertyStr(c, g, "framebufferRenderbuffer", JS_NewCFunction(c, nfu_gl_delete, "framebufferRenderbuffer", 4));
    JS_SetPropertyStr(c, g, "createRenderbuffer", JS_NewCFunction(c, nfu_gl_delete, "createRenderbuffer", 0));
    JS_SetPropertyStr(c, g, "bindRenderbuffer", JS_NewCFunction(c, nfu_gl_delete, "bindRenderbuffer", 2));
    JS_SetPropertyStr(c, g, "renderbufferStorage", JS_NewCFunction(c, nfu_gl_delete, "renderbufferStorage", 4));
    JS_SetPropertyStr(c, g, "renderbufferStorageMultisample", JS_NewCFunction(c, nfu_gl_delete, "renderbufferStorageMultisample", 5));
    JS_SetPropertyStr(c, g, "blitFramebuffer", JS_NewCFunction(c, nfu_gl_delete, "blitFramebuffer", 10));
    JS_SetPropertyStr(c, g, "invalidateFramebuffer", JS_NewCFunction(c, nfu_gl_delete, "invalidateFramebuffer", 2));
    JS_SetPropertyStr(c, g, "checkFramebufferStatus", JS_NewCFunction(c, nfu_getFramebufferStatus, "checkFramebufferStatus", 1));
    JS_SetPropertyStr(c, g, "COLOR", JS_NewInt32(c, 0x1800));
    JS_SetPropertyStr(c, g, "DEPTH", JS_NewInt32(c, 0x1801));
    JS_SetPropertyStr(c, g, "STENCIL", JS_NewInt32(c, 0x1802));
    // ---- WebGL enum constants ----
    // Every one of these was missing, so a call like drawArrays(gl.TRIANGLES,
    // ...) or clear(gl.COLOR_BUFFER_BIT) arrived at the bridge as undefined and
    // JS_ToFloat64 turned it into 0 (= POINTS / "clear nothing"). The engine
    // submitted thousands of draws that were all degenerate. The table below is
    // the WebGL 1.0 constant set plus the handful of WebGL 2 names babylon
    // reads, so the numbers the page passes are the real GL enums.
    {
        static const struct { const char* n; int v; } k_gl_consts[] = {
            {"DEPTH_BUFFER_BIT", 0x00000100}, {"STENCIL_BUFFER_BIT", 0x00000400},
            {"COLOR_BUFFER_BIT", 0x00004000},
            {"POINTS", 0x0000}, {"LINES", 0x0001}, {"LINE_LOOP", 0x0002},
            {"LINE_STRIP", 0x0003}, {"TRIANGLES", 0x0004},
            {"TRIANGLE_STRIP", 0x0005}, {"TRIANGLE_FAN", 0x0006},
            {"ZERO", 0}, {"ONE", 1},
            {"SRC_COLOR", 0x0300}, {"ONE_MINUS_SRC_COLOR", 0x0301},
            {"SRC_ALPHA", 0x0302}, {"ONE_MINUS_SRC_ALPHA", 0x0303},
            {"DST_ALPHA", 0x0304}, {"ONE_MINUS_DST_ALPHA", 0x0305},
            {"DST_COLOR", 0x0306}, {"ONE_MINUS_DST_COLOR", 0x0307},
            {"SRC_ALPHA_SATURATE", 0x0308},
            {"FUNC_ADD", 0x8006}, {"BLEND_EQUATION", 0x8009},
            {"BLEND_EQUATION_RGB", 0x8009}, {"BLEND_EQUATION_ALPHA", 0x883D},
            {"FUNC_SUBTRACT", 0x800A}, {"FUNC_REVERSE_SUBTRACT", 0x800B},
            {"BLEND_DST_RGB", 0x80C8}, {"BLEND_SRC_RGB", 0x80C9},
            {"BLEND_DST_ALPHA", 0x80CA}, {"BLEND_SRC_ALPHA", 0x80CB},
            {"CONSTANT_COLOR", 0x8001}, {"ONE_MINUS_CONSTANT_COLOR", 0x8002},
            {"CONSTANT_ALPHA", 0x8003}, {"ONE_MINUS_CONSTANT_ALPHA", 0x8004},
            {"BLEND_COLOR", 0x8005},
            {"ARRAY_BUFFER", 0x8892}, {"ELEMENT_ARRAY_BUFFER", 0x8893},
            {"ARRAY_BUFFER_BINDING", 0x8894},
            {"ELEMENT_ARRAY_BUFFER_BINDING", 0x8895},
            {"STREAM_DRAW", 0x88E0}, {"STATIC_DRAW", 0x88E4},
            {"DYNAMIC_DRAW", 0x88E8},
            {"BUFFER_SIZE", 0x8764}, {"BUFFER_USAGE", 0x8765},
            {"CURRENT_VERTEX_ATTRIB", 0x8626},
            {"FRONT", 0x0404}, {"BACK", 0x0405}, {"FRONT_AND_BACK", 0x0408},
            {"CULL_FACE", 0x0BEE}, {"BLEND", 0x0BE2}, {"DITHER", 0x0BD0},
            {"STENCIL_TEST", 0x0B90}, {"DEPTH_TEST", 0x0B71},
            {"SCISSOR_TEST", 0x0C11}, {"POLYGON_OFFSET_FILL", 0x8037},
            {"SAMPLE_ALPHA_TO_COVERAGE", 0x809E}, {"SAMPLE_COVERAGE", 0x80A0},
            {"NO_ERROR", 0}, {"INVALID_ENUM", 0x0500},
            {"INVALID_VALUE", 0x0501}, {"INVALID_OPERATION", 0x0502},
            {"OUT_OF_MEMORY", 0x0505}, {"CONTEXT_LOST_WEBGL", 0x9242},
            {"CW", 0x0900}, {"CCW", 0x0901},
            {"LINE_WIDTH", 0x0B21},
            {"ALIASED_POINT_SIZE_RANGE", 0x846D},
            {"ALIASED_LINE_WIDTH_RANGE", 0x846E},
            {"CULL_FACE_MODE", 0x0B45}, {"FRONT_FACE", 0x0B46},
            {"DEPTH_RANGE", 0x0B70}, {"DEPTH_WRITEMASK", 0x0B72},
            {"DEPTH_CLEAR_VALUE", 0x0B73}, {"DEPTH_FUNC", 0x0B74},
            {"STENCIL_CLEAR_VALUE", 0x0B77}, {"STENCIL_FUNC", 0x0B92},
            {"STENCIL_FAIL", 0x0B94}, {"STENCIL_PASS_DEPTH_FAIL", 0x0B95},
            {"STENCIL_PASS_DEPTH_PASS", 0x0B96}, {"STENCIL_REF", 0x0B97},
            {"STENCIL_VALUE_MASK", 0x0B93}, {"STENCIL_WRITEMASK", 0x0B98},
            {"STENCIL_BACK_FUNC", 0x8800}, {"STENCIL_BACK_FAIL", 0x8801},
            {"STENCIL_BACK_PASS_DEPTH_FAIL", 0x8802},
            {"STENCIL_BACK_PASS_DEPTH_PASS", 0x8803},
            {"STENCIL_BACK_REF", 0x8CA3}, {"STENCIL_BACK_VALUE_MASK", 0x8CA4},
            {"STENCIL_BACK_WRITEMASK", 0x8CA5},
            {"VIEWPORT", 0x0BA2}, {"SCISSOR_BOX", 0x0C10},
            {"COLOR_CLEAR_VALUE", 0x0C22}, {"COLOR_WRITEMASK", 0x0C23},
            {"UNPACK_ALIGNMENT", 0x0CF5}, {"PACK_ALIGNMENT", 0x0D05},
            {"MAX_TEXTURE_SIZE", 0x0D33}, {"MAX_VIEWPORT_DIMS", 0x0D3A},
            {"SUBPIXEL_BITS", 0x0D50},
            {"RED_BITS", 0x0D52}, {"GREEN_BITS", 0x0D53}, {"BLUE_BITS", 0x0D54},
            {"ALPHA_BITS", 0x0D55}, {"DEPTH_BITS", 0x0D56},
            {"STENCIL_BITS", 0x0D57},
            {"POLYGON_OFFSET_UNITS", 0x2A00}, {"POLYGON_OFFSET_FACTOR", 0x8038},
            {"TEXTURE_BINDING_2D", 0x8069},
            {"SAMPLE_BUFFERS", 0x80A8}, {"SAMPLES", 0x80A9},
            {"SAMPLE_COVERAGE_VALUE", 0x80AA},
            {"SAMPLE_COVERAGE_INVERT", 0x80AB},
            {"COMPRESSED_TEXTURE_FORMATS", 0x86A3},
            {"DONT_CARE", 0x1100}, {"FASTEST", 0x1101}, {"NICEST", 0x1102},
            {"GENERATE_MIPMAP_HINT", 0x8192},
            {"BYTE", 0x1400}, {"UNSIGNED_BYTE", 0x1401}, {"SHORT", 0x1402},
            {"UNSIGNED_SHORT", 0x1403}, {"INT", 0x1404},
            {"UNSIGNED_INT", 0x1405}, {"FLOAT", 0x1406},
            {"DEPTH_COMPONENT", 0x1902}, {"ALPHA", 0x1906}, {"RGB", 0x1907},
            {"RGBA", 0x1908}, {"LUMINANCE", 0x1909}, {"LUMINANCE_ALPHA", 0x190A},
            {"UNSIGNED_SHORT_4_4_4_4", 0x8033},
            {"UNSIGNED_SHORT_5_5_5_1", 0x8034},
            {"UNSIGNED_SHORT_5_6_5", 0x8363},
            {"FRAGMENT_SHADER", 0x8B30}, {"VERTEX_SHADER", 0x8B31},
            {"MAX_VERTEX_ATTRIBS", 0x8869},
            {"MAX_VERTEX_UNIFORM_VECTORS", 0x8DFB},
            {"MAX_VARYING_VECTORS", 0x8DFC},
            {"MAX_COMBINED_TEXTURE_IMAGE_UNITS", 0x8B4D},
            {"MAX_VERTEX_TEXTURE_IMAGE_UNITS", 0x8B4C},
            {"MAX_TEXTURE_IMAGE_UNITS", 0x8872},
            {"MAX_FRAGMENT_UNIFORM_VECTORS", 0x8DFD},
            {"SHADER_TYPE", 0x8B4F}, {"DELETE_STATUS", 0x8B80},
            {"LINK_STATUS", 0x8B82}, {"VALIDATE_STATUS", 0x8B83},
            {"ATTACHED_SHADERS", 0x8B85}, {"ACTIVE_UNIFORMS", 0x8B86},
            {"ACTIVE_ATTRIBUTES", 0x8B89},
            {"CURRENT_PROGRAM", 0x8B8D},
            {"NEVER", 0x0200}, {"LESS", 0x0201}, {"EQUAL", 0x0202},
            {"LEQUAL", 0x0203}, {"GREATER", 0x0204}, {"NOTEQUAL", 0x0205},
            {"GEQUAL", 0x0206}, {"ALWAYS", 0x0207},
            {"KEEP", 0x1E00}, {"REPLACE", 0x1E01}, {"INCR", 0x1E02},
            {"DECR", 0x1E03}, {"INVERT", 0x150A},
            {"INCR_WRAP", 0x8507}, {"DECR_WRAP", 0x8508},
            {"NEAREST", 0x2600}, {"LINEAR", 0x2601},
            {"NEAREST_MIPMAP_NEAREST", 0x2700},
            {"LINEAR_MIPMAP_NEAREST", 0x2701},
            {"NEAREST_MIPMAP_LINEAR", 0x2702},
            {"LINEAR_MIPMAP_LINEAR", 0x2703},
            {"TEXTURE_MAG_FILTER", 0x2800}, {"TEXTURE_MIN_FILTER", 0x2801},
            {"TEXTURE_WRAP_S", 0x2802}, {"TEXTURE_WRAP_T", 0x2803},
            {"TEXTURE_2D", 0x0DE1}, {"TEXTURE", 0x1702},
            {"TEXTURE_CUBE_MAP", 0x8513},
            {"TEXTURE_BINDING_CUBE_MAP", 0x8514},
            {"TEXTURE_CUBE_MAP_POSITIVE_X", 0x8515},
            {"TEXTURE_CUBE_MAP_NEGATIVE_X", 0x8516},
            {"TEXTURE_CUBE_MAP_POSITIVE_Y", 0x8517},
            {"TEXTURE_CUBE_MAP_NEGATIVE_Y", 0x8518},
            {"TEXTURE_CUBE_MAP_POSITIVE_Z", 0x8519},
            {"TEXTURE_CUBE_MAP_NEGATIVE_Z", 0x851A},
            {"MAX_CUBE_MAP_TEXTURE_SIZE", 0x851C},
            {"ACTIVE_TEXTURE", 0x84E0},
            {"REPEAT", 0x2901}, {"CLAMP_TO_EDGE", 0x812F},
            {"MIRRORED_REPEAT", 0x8370},
            {"FLOAT_VEC2", 0x8B50}, {"FLOAT_VEC3", 0x8B51},
            {"FLOAT_VEC4", 0x8B52}, {"INT_VEC2", 0x8B53},
            {"INT_VEC3", 0x8B54}, {"INT_VEC4", 0x8B55},
            {"BOOL", 0x8B56}, {"BOOL_VEC2", 0x8B57},
            {"BOOL_VEC3", 0x8B58}, {"BOOL_VEC4", 0x8B59},
            {"FLOAT_MAT2", 0x8B5A}, {"FLOAT_MAT3", 0x8B5B},
            {"FLOAT_MAT4", 0x8B5C}, {"SAMPLER_2D", 0x8B5E},
            {"SAMPLER_CUBE", 0x8B60},
            {"VERTEX_ATTRIB_ARRAY_ENABLED", 0x8622},
            {"VERTEX_ATTRIB_ARRAY_SIZE", 0x8623},
            {"VERTEX_ATTRIB_ARRAY_STRIDE", 0x8624},
            {"VERTEX_ATTRIB_ARRAY_TYPE", 0x8625},
            {"VERTEX_ATTRIB_ARRAY_NORMALIZED", 0x886A},
            {"VERTEX_ATTRIB_ARRAY_POINTER", 0x8645},
            {"VERTEX_ATTRIB_ARRAY_BUFFER_BINDING", 0x889F},
            {"IMPLEMENTATION_COLOR_READ_TYPE", 0x8B9A},
            {"IMPLEMENTATION_COLOR_READ_FORMAT", 0x8B9B},
            {"COMPILE_STATUS", 0x8B81},
            {"LOW_FLOAT", 0x8DF0}, {"MEDIUM_FLOAT", 0x8DF1},
            {"HIGH_FLOAT", 0x8DF2}, {"LOW_INT", 0x8DF3},
            {"MEDIUM_INT", 0x8DF4}, {"HIGH_INT", 0x8DF5},
            {"FRAMEBUFFER", 0x8D40}, {"RENDERBUFFER", 0x8D41},
            {"RGBA4", 0x8056}, {"RGB5_A1", 0x8057}, {"RGB565", 0x8D62},
            {"DEPTH_COMPONENT16", 0x81A5}, {"STENCIL_INDEX8", 0x8D48},
            {"DEPTH_STENCIL", 0x84F9},
            {"RENDERBUFFER_WIDTH", 0x8D42}, {"RENDERBUFFER_HEIGHT", 0x8D43},
            {"RENDERBUFFER_INTERNAL_FORMAT", 0x8D44},
            {"RENDERBUFFER_RED_SIZE", 0x8D50},
            {"FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE", 0x8CD0},
            {"FRAMEBUFFER_ATTACHMENT_OBJECT_NAME", 0x8CD1},
            {"FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL", 0x8CD2},
            {"FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE", 0x8CD3},
            {"COLOR_ATTACHMENT0", 0x8CE0},
            {"DEPTH_ATTACHMENT", 0x8D00},
            {"STENCIL_ATTACHMENT", 0x8D20},
            {"DEPTH_STENCIL_ATTACHMENT", 0x821A},
            {"NONE", 0},
            {"FRAMEBUFFER_COMPLETE", 0x8CD5},
            {"FRAMEBUFFER_INCOMPLETE_ATTACHMENT", 0x8CD6},
            {"FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT", 0x8CD7},
            {"FRAMEBUFFER_INCOMPLETE_DIMENSIONS", 0x8CD9},
            {"FRAMEBUFFER_UNSUPPORTED", 0x8CDD},
            {"FRAMEBUFFER_BINDING", 0x8CA6},
            {"RENDERBUFFER_BINDING", 0x8CA7},
            {"MAX_RENDERBUFFER_SIZE", 0x84E8},
            {"INVALID_FRAMEBUFFER_OPERATION", 0x0506},
            {"UNPACK_FLIP_Y_WEBGL", 0x9240},
            {"UNPACK_PREMULTIPLY_ALPHA_WEBGL", 0x9241},
            {"UNPACK_COLORSPACE_CONVERSION_WEBGL", 0x9243},
            {"BROWSER_DEFAULT_WEBGL", 0x9244},
        };
        for (size_t gi = 0; gi < sizeof(k_gl_consts) / sizeof(k_gl_consts[0]); gi++)
            JS_SetPropertyStr(c, g, k_gl_consts[gi].n, JS_NewInt32(c, k_gl_consts[gi].v));
        for (int ti = 0; ti < 32; ti++) {
            char nm[16];
            snprintf(nm, sizeof(nm), "TEXTURE%d", ti);
            JS_SetPropertyStr(c, g, nm, JS_NewInt32(c, 0x84C0 + ti));
        }
    }
    // drawing buffer size: engines read these PROPERTIES in viewport/aspect
    // math, so report the canvas' real size (the cid carries it as "@WxH")
    // instead of a fixed 800x600 - a mismatch skews every projection.
    {
        int cw = 800, ch = 600;
        const char* at = strrchr(cid, '@');
        if (at && sscanf(at + 1, "%dx%d", &cw, &ch) == 2 && cw > 0 && ch > 0) {
            /* parsed from the context id */
        } else {
            cw = 800; ch = 600;
        }
        JS_SetPropertyStr(c, g, "drawingBufferWidth", JS_NewInt32(c, cw));
        JS_SetPropertyStr(c, g, "drawingBufferHeight", JS_NewInt32(c, ch));
    }
    // enum constants the page reads
    JS_SetPropertyStr(c, g, "VENDOR", JS_NewInt32(c, 0x1F00));
    JS_SetPropertyStr(c, g, "RENDERER", JS_NewInt32(c, 0x1F01));
    JS_SetPropertyStr(c, g, "VERSION", JS_NewInt32(c, 0x1F02));
    JS_SetPropertyStr(c, g, "SHADING_LANGUAGE_VERSION", JS_NewInt32(c, 0x8B8C));
    JS_SetPropertyStr(c, g, "UNMASKED_VENDOR_WEBGL", JS_NewInt32(c, 0x9245));
    JS_SetPropertyStr(c, g, "UNMASKED_RENDERER_WEBGL", JS_NewInt32(c, 0x9246));
    JS_SetPropertyStr(c, g, "MAX_TEXTURE_SIZE", JS_NewInt32(c, 0x0D33));
    JS_SetPropertyStr(c, g, "MAX_VIEWPORT_DIMS", JS_NewInt32(c, 0x0D3A));
    JS_SetPropertyStr(c, g, "MAX_VERTEX_ATTRIBS", JS_NewInt32(c, 0x8869));
    JS_SetPropertyStr(c, g, "MAX_TEXTURE_IMAGE_UNITS", JS_NewInt32(c, 0x8073));
    JS_SetPropertyStr(c, g, "MAX_COMBINED_TEXTURE_IMAGE_UNITS", JS_NewInt32(c, 0x8B4D));
    JS_SetPropertyStr(c, g, "MAX_RENDERBUFFER_SIZE", JS_NewInt32(c, 0x84E8));
    JS_SetPropertyStr(c, g, "MAX_VERTEX_UNIFORM_VECTORS", JS_NewInt32(c, 0x8DFB));
    JS_SetPropertyStr(c, g, "MAX_FRAGMENT_UNIFORM_VECTORS", JS_NewInt32(c, 0x8DFD));
    JS_SetPropertyStr(c, g, "MAX_VARYING_VECTORS", JS_NewInt32(c, 0x8DFC));
    JS_SetPropertyStr(c, g, "MAX_TEXTURE_MAX_ANISOTROPY_EXT", JS_NewInt32(c, 0x84FF));
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
// Thread that created the runtime. quickjs contexts must only be entered from
// it; a mismatch is the classic source of heap corruption here.
static unsigned long s_js_tid = 0;

// Verbose host diagnostics are opt-in via NF_JS_TRACE=1 so normal runs stay
// quiet; real errors still print unconditionally.
static bool js_trace_enabled() {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("NF_JS_TRACE");
        v = (e && *e && *e != '0') ? 1 : 0;
    }
    return v == 1;
}

// Diagnostic: build a context step by step on a throwaway runtime and report
// which intrinsic (or allocation) fails. Host-only troubleshooting aid.
static void js_diag_init(JSRuntime* rt) {
    void* hp = malloc(1 << 20);
    if (js_trace_enabled()) { fprintf(stderr, "js: diag malloc1M=%p\n", hp); fflush(stderr); }
    if (hp) free(hp);
    JSContext* raw = JS_NewContextRaw(rt);
    if (js_trace_enabled()) { fprintf(stderr, "js: diag raw=%p\n", (void*)raw); fflush(stderr); }
    if (!raw) return;
    char r[24]; int n = 0;
    r[n++] = JS_AddIntrinsicBaseObjects(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicDate(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicEval(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicStringNormalize(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicRegExp(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicJSON(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicProxy(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicMapSet(raw) ? 'X' : 'o';
    r[n++] = JS_AddIntrinsicTypedArrays(raw) ? 'X' : 'o';
    {
        int e = JS_AddIntrinsicPromise(raw);
        r[n++] = e ? 'X' : 'o';
        if (e) {
            JSValue ex = JS_GetException(raw);
            const char* m = JS_ToCString(raw, ex);
            if (js_trace_enabled()) { fprintf(stderr, "js: promise intrinsic err=%s\n", m ? m : "(none)"); fflush(stderr); }
            if (m) JS_FreeCString(raw, m);
            JS_FreeValue(raw, ex);
        }
    }
    r[n++] = JS_AddIntrinsicWeakRef(raw) ? 'X' : 'o';
    r[n] = 0;
    if (js_trace_enabled()) { fprintf(stderr, "js: diag intrinsics=%s (X=fail)\n", r); fflush(stderr); }
    JS_FreeContext(raw);
}

static void ensure_js() {
    // Recover from a half-initialized runtime: if a previous JS_NewContext
    // failed and left s_rt behind, the early return below would keep s_ctx
    // null forever and every script would silently do nothing.
    if (s_rt && !s_ctx) { JS_FreeRuntime(s_rt); s_rt = 0; }
    if (s_rt) {
#ifndef NEFU_BARE
        unsigned long cur = (unsigned long)GetCurrentThreadId();
        if (s_js_tid && cur != s_js_tid)
            fprintf(stderr, "js: CROSS-THREAD use of ctx owner=%lu now=%lu\n",
                    s_js_tid, cur);
#endif
        return;
    }
    s_rt = getenv("NF_JS_GUARD") ? JS_NewRuntime2(&nfu_gd_mf, NULL) : JS_NewRuntime();
    if (js_trace_enabled()) { fprintf(stderr, "js: NewRuntime -> %p\n", (void*)s_rt); fflush(stderr); }
    if (!s_rt) return;
    // Unhandled promise rejections are invisible otherwise: bloxd's boot gates
    // hang on silently-rejected chains (webpack Qi.e() chunk factories that
    // throw during module body eval). Log the reason so the gate is diagnosable.
    JS_SetHostPromiseRejectionTracker(s_rt, [](JSContext* ctx, JSValueConst promise,
                                               JSValueConst reason, JS_BOOL is_handled, void* opaque) {
        (void)promise; (void)opaque;
        if (is_handled) return;
        const char* m = JS_ToCString(ctx, reason);
        fprintf(stderr, "js: UNHANDLED-REJECT: %s\n", m ? m : "(unstringifiable)");
        if (m) JS_FreeCString(ctx, m);
        // error .stack pinpoints the failing call site in minified bundles
        JSValue st = JS_GetPropertyStr(ctx, reason, "stack");
        if (JS_IsString(st)) {
            const char* s = JS_ToCString(ctx, st);
            if (s) {
                fprintf(stderr, "  at %.700s\n", s);
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, st);
        fflush(stderr);
    }, NULL);
    // ES module loader: import() from page code fetches chunks over HTTP.
    JS_SetModuleLoaderFunc(s_rt, nfu_mod_normalize, nfu_mod_loader, NULL);
    // JS stack budget: the guard that turns runaway recursion into a
    // catchable RangeError instead of a hang. It is configurable because the
    // right value differs wildly between the two targets - the Win32 host
    // has a 256 MB worker stack, the bare kernel only has the few MB that
    // are left between the kernel image and the arena.
    //
    // The value matters a lot in practice: bloxd's obfuscated deobfuscator
    // recurses deeply, and with too small a budget it overflows the guard,
    // the obfuscator catches the RangeError and rethrows it as its own
    // "Lava has 8 animation states" error - so a stack that is too small
    // looks like a corrupt site rather than like an out-of-stack error.
    {
        const char* senv = getenv("NF_JS_STACK_KB");
        uint32_t kb = (senv && *senv) ? (uint32_t)strtoll(senv, 0, 10) : 0;
        if (kb) {
            JS_SetMaxStackSize(s_rt, kb * 1024);
        } else {
#ifdef NEFU_BARE
    // Bare kernel: no per-thread guard page, so JS_NewRuntime2's default
    // stack_limit would wrap on our stack and reject every call. The kernel
    // stack region is a few MB, so the budget is deliberately modest.
    JS_SetMaxStackSize(s_rt, 4 * 1024 * 1024);
#else
    // Win32 host: bound the JS stack so runaway sync recursion throws
    // RangeError instead of growing into an uninterruptible hang. The limit
    // is measured from the stack top captured at runtime creation; it MUST
    // stay below the actual thread stack (page-loader workers get 256MB via
    // platform_thread_create_stk; the linker reserves 256MB for the main
    // thread). 128MB let bloxd's obfuscator — running deep in the interpreter
    // — call eval() mid-execution, and the parser's C recursion stacked on
    // top of the interpreter's C frames blew the 256MB thread stack in
    // put_short_code/put_lvalue. 48MB leaves ~200MB of headroom for the
    // parser while still giving the deobfuscator plenty of JS depth.
    JS_SetMaxStackSize(s_rt, 48 * 1024 * 1024);
#endif
        }
    }
    // size_t arithmetic: 2048*1024*1024 as a signed int overflows to a
    // negative value (UB), so force the literal into 64-bit before multiply.
    JS_SetMemoryLimit(s_rt, ((size_t)2048) * 1024 * 1024);  // 2GB: avoid runaway GC stalls on big bundles
    JS_SetGCThreshold(s_rt, ((size_t)1024) * 1024 * 1024);  // only auto-GC past 1GB (React render churn)
    s_ctx = JS_NewContext(s_rt);
    if (!s_ctx) {
        // Intrinsic setup can fail when the runtime was configured with a
        // stack/memory bound that the current thread cannot honour (the page
        // loader runs on a worker thread with its own stack). Fall back to a
        // stock runtime so the engine still comes up.
        if (js_trace_enabled()) { fprintf(stderr, "js: NewContext failed with limits, retrying defaults\n"); fflush(stderr); }
        JS_FreeRuntime(s_rt);
        s_rt = getenv("NF_JS_GUARD") ? JS_NewRuntime2(&nfu_gd_mf, NULL) : JS_NewRuntime();
        if (s_rt) s_ctx = JS_NewContext(s_rt);
        if (js_trace_enabled()) { fprintf(stderr, "js: default retry ctx=%p\n", (void*)s_ctx); fflush(stderr); }
        if (!s_ctx && s_rt) js_diag_init(s_rt);
    }
    if (!s_ctx) { if (s_rt) { JS_FreeRuntime(s_rt); s_rt = 0; } return; }
#ifndef NEFU_BARE
    s_js_tid = (unsigned long)GetCurrentThreadId();
#endif
    if (js_trace_enabled()) { fprintf(stderr, "js: ctx created tid=%lu\n", s_js_tid); fflush(stderr); }
    JSValue g = JS_GetGlobalObject(s_ctx);
    JS_SetPropertyStr(s_ctx, g, "__nfuSync",
        JS_NewCFunction(s_ctx, nfu_nativeSync, "__nfuSync", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuHttpGet",
        JS_NewCFunction(s_ctx, nfu_httpGet, "__nfuHttpGet", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuBitmapPixels",
        JS_NewCFunction(s_ctx, nfu_bitmapPixels, "__nfuBitmapPixels", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuHttpRequest",
        JS_NewCFunction(s_ctx, nfu_httpRequest, "__nfuHttpRequest", 4));
    JS_SetPropertyStr(s_ctx, g, "__nfuEvalGlobal",
        JS_NewCFunction(s_ctx, nfu_evalGlobal, "__nfuEvalGlobal", 3));
    JS_SetPropertyStr(s_ctx, g, "__nfuExtendDeadline",
        JS_NewCFunction(s_ctx, nfu_extendDeadline, "__nfuExtendDeadline", 1));
    // Turnstile-skip hint: true when a traffic code is served locally.
    JS_SetPropertyStr(s_ctx, g, "__nfuTrafficSkip",
        JS_NewCFunction(s_ctx, nfu_trafficSkip, "__nfuTrafficSkip", 0));
    // Cookie jar seed for a real browser identity (see nfu_cookie_seed).
    JS_SetPropertyStr(s_ctx, g, "__nfuCookieSeed",
        JS_NewCFunction(s_ctx, nfu_cookieSeed, "__nfuCookieSeed", 0));
    // Native copyWithin for typed arrays (see nfu_ta_copywithin).
    JS_SetPropertyStr(s_ctx, g, "__nfuTACopyWithin",
        JS_NewCFunction(s_ctx, nfu_ta_copywithin, "__nfuTACopyWithin", 3));
    // Local wasm module cache (see nfu_localWasm / nfu_readFile).
    // Anonymous identity blob captured from a real browser session (see
    // nfu_metrics_body).
    JS_SetPropertyStr(s_ctx, g, "__nfuMetricsBody",
        JS_NewCFunction(s_ctx, nfu_metricsBody, "__nfuMetricsBody", 0));
    // Exact 3PSIDMCSP token seen on a successful social request (see nfu_mcsp).
    JS_SetPropertyStr(s_ctx, g, "__nfuMcsp",
        JS_NewCFunction(s_ctx, nfu_mcspValue, "__nfuMcsp", 0));
    // Accepted analytics bundle for the room join (see nfu_gencook).
    JS_SetPropertyStr(s_ctx, g, "__nfuGenCook",
        JS_NewCFunction(s_ctx, nfu_gencookValue, "__nfuGenCook", 0));
    // Full metric identity object for Colyseus join bodies (see
    // nfu_metrics_cookies): jar cookie + identity blob + session proof.
    JS_SetPropertyStr(s_ctx, g, "__nfuMetricsCookies",
        JS_NewCFunction(s_ctx, nfu_metricsCookiesValue, "__nfuMetricsCookies", 0));
    JS_SetPropertyStr(s_ctx, g, "__nfuLocalWasm",
        JS_NewCFunction(s_ctx, nfu_localWasm, "__nfuLocalWasm", 0));
    JS_SetPropertyStr(s_ctx, g, "__nfuReadFile",
        JS_NewCFunction(s_ctx, nfu_readFile, "__nfuReadFile", 1));
    // Native UTF-8 codec so TextEncoder/TextDecoder never burn the JS budget on
    // large payloads (bloxd room messages are hundreds of KB per frame).
    JS_SetPropertyStr(s_ctx, g, "__nfuUtf8Enc",
        JS_NewCFunction(s_ctx, nfu_utf8enc, "__nfuUtf8Enc", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuUtf8Dec",
        JS_NewCFunction(s_ctx, nfu_utf8dec, "__nfuUtf8Dec", 1));
    // Navigation intent (location.href=, reload, back, form submit)
    JS_SetPropertyStr(s_ctx, g, "__nfuGo",
        JS_NewCFunction(s_ctx, nfu_go, "__nfuGo", 2));
    JS_SetPropertyStr(s_ctx, g, "__nfuGoPost",
        JS_NewCFunction(s_ctx, nfu_goPost, "__nfuGoPost", 2));
    JS_SetPropertyStr(s_ctx, g, "__getGL",
        JS_NewCFunction(s_ctx, nfu_getGL, "__getGL", 1));
    // WebAssembly over wasm3. bloxd.io aborts its whole boot with
    // "Bloxd couldn't start in this browser" without a WebAssembly global,
    // and its engine core is the Rust physics module it fetches at runtime.
    if (!wasm_js_install(s_ctx, g) && js_trace_enabled()) {
        fprintf(stderr, "js: WebAssembly install failed\n");
        fflush(stderr);
    }
    // WebSocket bridge (real RFC 6455 client in the platform backend)
    JS_SetPropertyStr(s_ctx, g, "__nfuWsOpen",
        JS_NewCFunction(s_ctx, nfu_wsOpen, "__nfuWsOpen", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuWsState",
        JS_NewCFunction(s_ctx, nfu_wsState, "__nfuWsState", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuWsSend",
        JS_NewCFunction(s_ctx, nfu_wsSend, "__nfuWsSend", 2));
    JS_SetPropertyStr(s_ctx, g, "__nfuWsPoll",
        JS_NewCFunction(s_ctx, nfu_wsPoll, "__nfuWsPoll", 1));
    JS_SetPropertyStr(s_ctx, g, "__nfuWsClose",
        JS_NewCFunction(s_ctx, nfu_wsClose, "__nfuWsClose", 1));
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
    // Layout viewport seen by JS. NF_VIEW_W/NF_VIEW_H is the same override the
    // layout engine honours, so script-side media decisions (bloxd's portrait
    // card grid) agree with the geometry we actually lay out.
    int jvw = s_env_w, jvh = s_env_h;
    if (jvw <= 0) {
        const char* e = getenv("NF_VIEW_W");
        if (e && e[0]) jvw = atoi(e);
    }
    if (jvh <= 0) {
        const char* e = getenv("NF_VIEW_H");
        if (e && e[0]) jvh = atoi(e);
    }
    JS_SetPropertyStr(s_ctx, wnd, "innerWidth", JS_NewInt32(s_ctx, jvw > 0 ? jvw : 1024));
    JS_SetPropertyStr(s_ctx, wnd, "innerHeight", JS_NewInt32(s_ctx, jvh > 0 ? jvh : 768));
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
    // Backing field of the shim's href accessor (the accessor is installed when
    // the shim runs; its setter requests a navigation, which must not happen
    // for the initial address).
    JS_SetPropertyStr(s_ctx, loc, "__href", JS_NewString(s_ctx, envurl));
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
    // Must equal the UA on the wire (see kChromeUa in backends/win32). A page
    // that sees "nefuOS/0.2 (host)" here while the request claimed Chrome
    // concludes it is being scripted and serves a challenge instead of content.
    JS_SetPropertyStr(s_ctx, nav, "userAgent", JS_NewString(s_ctx, kNfuUaJs));
    JS_SetPropertyStr(s_ctx, nav, "appName", JS_NewString(s_ctx, "Netscape"));
    JS_SetPropertyStr(s_ctx, nav, "appVersion", JS_NewString(s_ctx, "5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"));
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
    // Diagnostic switch for the Cloudflare challenge frame: the frame script is
    // minified/obfuscated, so the only way to identify the API its failure
    // points at is to dump the exact text the runtime executed and cut at the
    // reported line:column. Off unless NF_DUMP_FRAME is set.
    {
        const char* df = getenv("NF_DUMP_FRAME");
        JS_SetPropertyStr(s_ctx, g, "__nfuDumpFrame",
                          JS_NewBool(s_ctx, (df && df[0] && df[0] != '0') ? 1 : 0));
    }
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
    // Transport patch: override XMLHttpRequest/fetch so POST auth + Colyseus
    // matchmaking carry a real method/body/headers (see jsdom_shim.h).
    JSValue tp = JS_Eval(s_ctx, NFU_TRANSPORT_PATCH, (size_t)strlen(NFU_TRANSPORT_PATCH),
                          "<transport-patch>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(tp)) {
        JSValue ex = JS_GetException(s_ctx);
        const char* m = JS_ToCString(s_ctx, ex);
        if (m) { fprintf(stderr, "transport patch error: %s\n", m); JS_FreeCString(s_ctx, m); }
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, tp);
    // minimal console so bundled apps (bloxd etc.) don't die on console.log;
    // log/warn/error route through print so the C-side rate-limited probe can
    // trace where a bundle stalls (empty functions would hide it entirely).
    // Depth-limited object dumper for console. bloxd logs its loading state as
    // objects; a plain join() turned every one of them into an empty line, which
    // hid the whole state machine behind a row of blank log# entries.
    const char* consfmt = R"js(
function __ncFmt(v, d){
  if (v === null) return 'null';
  var t = typeof v;
  if (t !== 'object' && t !== 'function') return String(v);
  if (t === 'function') return '[fn]';
  if (d > 2) return '...';
  if (v instanceof Error) return 'Err(' + (v.message || '') + ')';
  if (v instanceof ArrayBuffer) return '[ab ' + v.byteLength + ']';
  try {
    if (Object.prototype.toString.call(v) === '[object Array]'){
      var a = [], n = v.length < 10 ? v.length : 10;
      for (var i = 0; i < n; i++) a.push(__ncFmt(v[i], d + 1));
      return '[' + a.join(',') + (v.length > 10 ? ',..' : '') + ']';
    }
    if (v.byteLength && v.length !== undefined && v.buffer) return '[' + v.length + 'b]';
    var ks = Object.keys(v), o = [], n2 = ks.length < 14 ? ks.length : 14;
    for (var j = 0; j < n2; j++){
      var s; try { s = __ncFmt(v[ks[j]], d + 1); } catch(e){ s = '?'; }
      o.push(ks[j] + ':' + s);
    }
    return '{' + o.join(',') + (ks.length > 14 ? ',..' : '') + '}';
  } catch(e2){ return '[obj]'; }
}
function __ncJoin(a){
  var r = [], i;
  for (i = 0; i < a.length; i++){ var s; try { s = __ncFmt(a[i], 0); } catch(e){ s = '?'; } r.push(s); }
  var out = r.join(' ');
  return out.length > 420 ? out.substring(0, 420) + '..' : out;
}
)js";
    JSValue cfe = JS_Eval(s_ctx, consfmt, (size_t)strlen(consfmt), "<console-fmt>",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(cfe)) {
        JSValue ex = JS_GetException(s_ctx);
        const char* m = JS_ToCString(s_ctx, ex);
        if (m) { fprintf(stderr, "console fmt error: %s\n", m); JS_FreeCString(s_ctx, m); }
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, cfe);
    const char* consrc =
        "if (typeof console === 'undefined') console = {};"
        "  console.log = function(){ print(__ncJoin(Array.prototype.slice.call(arguments))); };"
        "  console.info = console.log; console.warn = console.log;"
        "  console.error = function(){ var a = Array.prototype.slice.call(arguments);"
        "    try { if (a[0] && a[0].stack) { var fr = String(a[0].stack).split('\\n');"
        "      for (var q = 0; q < 10 && q < fr.length; q++) { var _fs = fr[q];"
        "        var _fn = Math.ceil(_fs.length / 180) || 1;"
        "        for (var _fw = 0; _fw < _fn; _fw++) print('[ST' + q + '.' + _fw + '] ' + _fs.substr(_fw * 180, 180)); } } } catch(x){}"
        "    try { if (a[0] && a[0].name) print('[STNM] ' + a[0].name + ' msg=' + String(a[0].message || '(none)')); } catch(y){}"
        "    print('[MARK][ERR] ' + __ncJoin(a)); };"
        "  console.debug = function(){}; console.trace = function(){};"
        "  console.assert = function(){}; console.count = function(){}; console.table = function(){};";
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
if (typeof TextEncoder === 'undefined') { TextEncoder = function(){ this.encoding = 'utf-8'; }; TextEncoder.prototype.encode = function(s){
  s = String(s == null ? '' : s);
  var out = [];
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    // surrogate pair -> single astral code point (4-byte UTF-8); lone
    // surrogate -> U+FFFD, matching WHATWG TextEncoder exactly so bytes
    // round-trip through our TextDecoder (which already does 4-byte).
    if (c >= 0xD800 && c <= 0xDBFF) {
      var d = (i + 1 < s.length) ? s.charCodeAt(i + 1) : 0;
      if (d >= 0xDC00 && d <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; }
      else { c = 0xFFFD; }
    } else if (c >= 0xDC00 && c <= 0xDFFF) { c = 0xFFFD; }
    if (c < 128) out.push(c);
    else if (c < 2048) { out.push(192|(c>>6), 128|(c&63)); }
    else if (c < 0x10000) { out.push(224|(c>>12), 128|((c>>6)&63), 128|(c&63)); }
    else { out.push(240|(c>>18), 128|((c>>12)&63), 128|((c>>6)&63), 128|(c&63)); }
  }
  return new Uint8Array(out);
}; }
if (typeof TextDecoder === 'undefined') { TextDecoder = function(enc){ this.encoding = enc || 'utf-8'; }; TextDecoder.prototype.decode = function(buf, o){
  if (buf == null) return '';
  var u8 = (buf instanceof Uint8Array) ? buf : new Uint8Array(buf);
  var n = u8.length, out = '', i = 0;
  // proper UTF-8 decode (WHATWG semantics): invalid bytes -> U+FFFD, so the
  // output matches real browsers exactly (obfuscator checksums rely on this)
  while (i < n) {
    var b = u8[i], cp = -1, seq = 1;
    if (b < 0x80) { cp = b; seq = 1; }
    else if ((b & 0xE0) === 0xC0 && i + 1 < n && (u8[i+1] & 0xC0) === 0x80) { cp = ((b & 0x1F) << 6) | (u8[i+1] & 0x3F); seq = 2; }
    else if ((b & 0xF0) === 0xE0 && i + 2 < n && (u8[i+1] & 0xC0) === 0x80 && (u8[i+2] & 0xC0) === 0x80) {
      cp = ((b & 0x0F) << 12) | ((u8[i+1] & 0x3F) << 6) | (u8[i+2] & 0x3F); seq = 3;
      if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = -1;
    } else if ((b & 0xF8) === 0xF0 && i + 3 < n && (u8[i+1] & 0xC0) === 0x80 && (u8[i+2] & 0xC0) === 0x80 && (u8[i+3] & 0xC0) === 0x80) {
      cp = ((b & 0x07) << 18) | ((u8[i+1] & 0x3F) << 12) | ((u8[i+2] & 0x3F) << 6) | (u8[i+3] & 0x3F); seq = 4;
      if (cp < 0x10000 || cp > 0x10FFFF) cp = -1;
    }
    if (cp >= 0) {
      if (cp < 0x10000) out += String.fromCharCode(cp);
      else { cp -= 0x10000; out += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)); }
      i += seq;
    } else { out += String.fromCharCode(0xFFFD); i += 1; }
  }
  return out;
}; }
if (typeof crypto === 'undefined') { crypto = { getRandomValues: function(arr){ for (var i=0;i<arr.length;i++) arr[i] = Math.floor(Math.random()*256); return arr; }, randomUUID: function(){ return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c){ var r = Math.random()*16|0, v = c=='x'?r:(r&0x3|0x8); return v.toString(16); }); }, subtle: {} }; }
if (typeof window !== 'undefined' && typeof window.crypto === 'undefined') window.crypto = crypto;
if (typeof document !== 'undefined' && document) { try { document.defaultView = (typeof window !== 'undefined') ? window : null; } catch(e){} }
if (!Object.hasOwn) Object.hasOwn = function(o, k){ return Object.prototype.hasOwnProperty.call(o, k); };
if (!Array.prototype.at) Array.prototype.at = function(i){ var n = this.length; i = i < 0 ? n + i : i; return (i < 0 || i >= n) ? undefined : this[i]; };
if (!String.prototype.replaceAll) String.prototype.replaceAll = function(a, b){ return this.split(a).join(b); };
if (typeof structuredClone === 'undefined') structuredClone = function(x){ try { return JSON.parse(JSON.stringify(x)); } catch(e){ return x; } };
if (typeof queueMicrotask === 'undefined') queueMicrotask = function(f){ try { Promise.resolve().then(f); } catch(e){} };
// MessageChannel: React's Scheduler (bloxd.io) does new MessageChannel() and
// assigns port1.onmessage — a bare constructor stub leaves port1 undefined
// and the whole app dies with "cannot set property 'onmessage' of undefined".
// Real semantics: postMessage delivers asynchronously to the peer port.
if (typeof MessageChannel === 'undefined') {
  window.MessageChannel = function() {
    function mkPort() {
      var p = {
        onmessage: null, onmessageerror: null,
        postMessage: function(msg) {
          var t = p._peer;
          // setTimeout, not Promise.then: React's scheduler loops through
          // this channel; microtask delivery would spin the job drain
          // forever with no exit to the macrotask world.
          var go = function() {
            if (t && typeof t.onmessage === 'function') {
              try { t.onmessage({ data: msg, ports: [] }); } catch(e) {}
            }
          };
          if (typeof setTimeout === 'function') setTimeout(go, 0);
          else Promise.resolve().then(go);
        },
        start: function(){}, close: function(){},
        addEventListener: function(t, f){ if (t === 'message' && typeof f === 'function') p.onmessage = f; },
        removeEventListener: function(t, f){ if (p.onmessage === f) p.onmessage = null; },
        dispatchEvent: function(){ return false; }
      };
      return p;
    }
    var p1 = mkPort(), p2 = mkPort();
    p1._peer = p2; p2._peer = p1;
    this.port1 = p1; this.port2 = p2;
  };
}
if (typeof window !== 'undefined' && typeof window.MessageChannel === 'undefined' && typeof MessageChannel !== 'undefined') window.MessageChannel = MessageChannel;
// EventTarget: bloxd.io's recaptcha-quieter patches EventTarget.prototype —
// the generic class stubs below are bare constructors with no prototype
// methods, which breaks both `extends EventTarget` and prototype patching.
if (typeof EventTarget === 'undefined') {
  window.EventTarget = function EventTarget(){};
  EventTarget.prototype.addEventListener = function(t, f){
    if (typeof f !== 'function') return;
    (this.__evts || (this.__evts = {}))[t] = (this.__evts[t] || []);
    this.__evts[t].push(f);
  };
  EventTarget.prototype.removeEventListener = function(t, f){
    var a = this.__evts && this.__evts[t];
    if (a) { var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); }
  };
  EventTarget.prototype.dispatchEvent = function(ev){
    var ty = ev && ev.type, a = this.__evts && this.__evts[ty];
    if (a) for (var i = 0; i < a.length; i++) { try { a[i].call(this, ev); } catch(e) {} }
    return true;
  };
}
if (typeof globalThis === 'undefined') globalThis = (typeof window !== 'undefined') ? window : this;
if (typeof window !== 'undefined' && window) { try {
  ['HTMLIFrameElement','HTMLElement','HTMLDivElement','HTMLCanvasElement','HTMLImageElement','HTMLInputElement','HTMLButtonElement','HTMLTextAreaElement','HTMLSelectElement','HTMLFormElement','HTMLAnchorElement','HTMLLinkElement','HTMLScriptElement','HTMLStyleElement','HTMLVideoElement','HTMLAudioElement','HTMLBodyElement','HTMLHeadElement','SVGElement','SVGSVGElement','Element','Node','Document','Window','Event','MouseEvent','KeyboardEvent','PointerEvent','TouchEvent','CustomEvent','MutationObserver','WebGLRenderingContext','WebGL2RenderingContext','WebGLActiveInfo','WebGLBuffer','WebGLFramebuffer','WebGLProgram','WebGLQuery','WebGLRenderbuffer','WebGLSampler','WebGLShader','WebGLSync','WebGLTexture','WebGLTransformFeedback','WebGLUniformLocation','WebGLVertexArrayObject','AudioContext','webkitAudioContext','OfflineAudioContext','Worker','SharedWorker','MediaSource','Blob','File','FileReader','FormData','ImageBitmap','RTCSessionDescription','PerformanceObserver','TextEncoder','TextDecoder','Notification','BroadcastChannel','MessageChannel','IDBKeyRange'].forEach(function(n){ if (typeof window[n] === 'undefined') { try { var T = function(){}; window[n] = T; } catch(e){} } });
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
      return {};
    }
    return __nfujp(s);
  };
} catch(e){} }
if (typeof JSON !== 'undefined' && JSON.stringify) { try {
  var __nfujs = JSON.stringify;
  JSON.stringify = __nfujs;
} catch(e){} }
// (timer overrides removed: an old workaround here replaced setTimeout with
// a microtask and disabled setInterval entirely, silently killing every
// polling/retry loop on every page — most visibly the Cloudflare Turnstile
// challenge. The DOM shim provides a real asynchronous timer queue drained
// by mini_js_tick; those semantics must stay in effect.)

if (typeof fetch === 'undefined') fetch = function(){ return new Promise(function(res, rej){ rej(new Error('fetch not supported')); }); };
if (typeof WebSocket === 'undefined') WebSocket = function(url, p){ this.readyState = 3; this.CONNECTING=0; this.OPEN=1; this.CLOSING=2; this.CLOSED=3; };
if (typeof XMLHttpRequest === 'undefined') XMLHttpRequest = function(){
  this.readyState=0; this.status=0; this.responseText=''; this.response='';
  this.open=function(m,u){ this._method=m; this._url=u; };
  this.setRequestHeader=function(){};
  this.send=function(body){
    var d='';
    try { if (this._url && typeof __nfuHttpGet === 'function') { d = __nfuHttpGet(this._url) || ''; } } catch(e){ d=''; }
    this.readyState=4; this.status=200; this.responseText=d; this.response=d; this.responseURL=this._url||'';
    if (typeof this.onreadystatechange==='function') { try { this.onreadystatechange(); } catch(e){} }
    if (typeof this.onload==='function') { try { this.onload(); } catch(e){} }
  };
};
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
  // Real formatting, not a hollow stub: pages call
  //   new Intl.DateTimeFormat('zh-CN', {...}).format(date)
  // and an empty string there looks exactly like "the clock is broken" to a
  // user. Local getters on purpose - that is the offset the clock stack hands
  // to Date.getHours(), so Intl and the taskbar agree.
  DateTimeFormat: function(locale, opts){
    opts = opts || {};
    var pad2 = function(n){ n = '' + n; return n.length < 2 ? '0' + n : n; };
    return {
      format: function(d){
        var x = (d === undefined || d === null) ? new Date() : (typeof d === 'number' ? new Date(d) : d);
        if (!(x instanceof Date)) { try { x = new Date(x); } catch(e) { return 'Invalid Date'; } }
        if (!x || isNaN(x.getTime())) return 'Invalid Date';
        var wantDate = opts.year || opts.month || opts.day;
        var wantTime = opts.hour || opts.minute || opts.second;
        if (!wantDate && !wantTime) { wantDate = true; wantTime = true; }
        var ds = x.getFullYear() + '-' + pad2(x.getMonth() + 1) + '-' + pad2(x.getDate());
        var ts = pad2(x.getHours()) + ':' + pad2(x.getMinutes()) + ':' + pad2(x.getSeconds());
        if (wantDate && !wantTime) return ds;
        if (wantTime && !wantDate) return ts;
        return ds + ' ' + ts;
      },
      formatToParts: function(d){
        var s = this.format(d), a = s.split(' ');
        return [{ type:'date', value:(a.length > 1 ? a[0] : s) }, { type:'literal', value:(a.length > 1 ? ' ' : '') }, { type:'time', value:(a.length > 1 ? a[1] : '') }];
      },
      resolvedOptions: function(){
        var off = -new Date().getTimezoneOffset();
        var neg = off < 0, mag = neg ? -off : off;
        var ah = Math.floor(mag / 60), am = mag - ah * 60;
        return { locale: locale || 'zh-CN', calendar: 'gregory', numberingSystem: 'latn',
                 timeZone: (!neg && ah === 8 && am === 0) ? 'Asia/Shanghai' : 'UTC' + (neg ? '-' : '+') + pad2(ah) + ':' + pad2(am) };
      }
    };
  },
  NumberFormat: function(locale, opts){
    opts = opts || {};
    return {
      format: function(n){
        var v = Number(n);
        if (!isFinite(v)) return '' + n;
        var neg = v < 0, a = neg ? -v : v;
        var s = '' + (Math.round(a * 100) / 100);
        var dot = s.indexOf('.'), ip = dot < 0 ? s : s.slice(0, dot), fp = dot < 0 ? '' : s.slice(dot);
        var g = '';
        for (var i = 0; i < ip.length; i++) {
          if (i && opts.useGrouping !== false && ((ip.length - i) - Math.floor((ip.length - i) / 3) * 3) === 0) g += ',';
          g += ip[i];
        }
        return (neg ? '-' : '') + g + fp;
      },
      resolvedOptions: function(){ return { locale: locale || 'zh-CN', useGrouping: opts.useGrouping !== false }; }
    };
  },
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
    // Only wrap when window is a SEPARATE object from globalThis. In this
    // environment window === globalThis, so window.Error IS the global Error
    // constructor: replacing it with _nfusafe (which itself does
    // `new Error(...)`) creates a self-referencing constructor and every
    // later `new Error` recurses until the stack dies (observed as a
    // 0xC0000005 inside nfu_out's error path on bloxd.io).
    if (Error && !Error.__nfuStackSafe && window !== globalThis) {
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
static volatile unsigned long s_js_lock_tid = 0;   // who holds it (diagnostics)
static inline void js_lock(void) {
    uint32_t lk0 = platform_tick_ms();
    while (__sync_lock_test_and_set(&s_js_lock, 1)) {
        // A long hold is NORMAL here: bloxd's consent click reloads the page
        // while the worker re-mounts React (10s+ of JS), and the loader thread
        // queues behind it. Force-releasing after 2s put TWO threads inside
        // quickjs and corrupted the heap (0xC0000005), so just wait it out.
        if (platform_tick_ms() - lk0 > 60000) {
            fprintf(stderr, "js: LOCK STUCK holder=%lu waiter=%lu\n",
                    s_js_lock_tid, (unsigned long)GetCurrentThreadId()); fflush(stderr);
            __sync_lock_release(&s_js_lock);
            lk0 = platform_tick_ms();
        }
        Sleep(1);   // yield: never busy-spin the whole core
    }
#ifndef NEFU_BARE
    s_js_lock_tid = (unsigned long)GetCurrentThreadId();
#endif
}
static inline void js_unlock(void) {
    s_js_lock_tid = 0;
    __sync_lock_release(&s_js_lock);
}

extern "C" USHORT NTAPI RtlCaptureStackBackTrace(ULONG FramesToSkip,
                                                  ULONG FramesToCapture,
                                                  PVOID* BackTrace,
                                                  PULONG BackTraceHash);

// Crash tracing uses only static storage: when the fault is a stack overflow
// the handler must not depend on the (exhausted) stack of the faulting thread.
static char g_crash_buf[1024];
static volatile LONG g_crash_busy = 0;

static LONG WINAPI nfu_veh(PEXCEPTION_POINTERS ep) {
    if (ep && ep->ExceptionRecord && (ep->ExceptionRecord->ExceptionCode & 0x80000000u)) {
        if (InterlockedExchange(&g_crash_busy, 1) == 0) {
            unsigned long long code = (unsigned long long)ep->ExceptionRecord->ExceptionCode;
            unsigned long long addr = (unsigned long long)ep->ExceptionRecord->ExceptionAddress;
            unsigned long long info0 = ep->ExceptionRecord->NumberParameters > 0
                                           ? ep->ExceptionRecord->ExceptionInformation[0] : 0;
            unsigned long long info1 = ep->ExceptionRecord->NumberParameters > 1
                                           ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
            unsigned long long rip = ep->ContextRecord ? (unsigned long long)ep->ContextRecord->Rip : 0;
            unsigned long long rcx = ep->ContextRecord ? (unsigned long long)ep->ContextRecord->Rcx : 0;
            unsigned long long rsp = ep->ContextRecord ? (unsigned long long)ep->ContextRecord->Rsp : 0;
            void* frames[12] = {0};
            USHORT nf = RtlCaptureStackBackTrace(0, 12, frames, 0);
            unsigned long long base = (unsigned long long)GetModuleHandleA(NULL);
            int bl = snprintf(g_crash_buf, sizeof(g_crash_buf),
                "CRASH code=%llx addr=%llx info0=%llx info1=%llx RIP=%llx RSP=%llx RCX=%llx tid=%lu\nSTACK:",
                code, addr, info0, info1, rip, rsp, rcx,
                (unsigned long)GetCurrentThreadId());
            for (USHORT i = 0; i < nf && i < 12 && bl > 0 && bl < (int)sizeof(g_crash_buf) - 40; i++) {
                unsigned long long f2 = (unsigned long long)frames[i];
                bl += snprintf(g_crash_buf + bl, (size_t)(sizeof(g_crash_buf) - (size_t)bl),
                               " %llx(off=%llx)", f2, base ? f2 - base : 0);
            }
            const char* tmp = getenv("TEMP");
            char crashpath[256];
            int pl = snprintf(crashpath, sizeof(crashpath), "%s\\nefu_js_crash.txt",
                              (tmp && tmp[0]) ? tmp : ".");
            (void)pl;
            HANDLE f = CreateFileA(crashpath, GENERIC_WRITE, FILE_SHARE_READ,
                                   NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD wb = 0;
                WriteFile(f, g_crash_buf, (DWORD)strlen(g_crash_buf), &wb, NULL);
                CloseHandle(f);
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#ifndef NEFU_BARE
#ifndef NEFU_BARE
static volatile RunCtx* g_tick_rc = 0;
// Names the bytecode function the JS thread is inside (see quickjs.c).
extern "C" const char* nfu_js_dbg_name(void);
extern "C" int nfu_js_dbg_line(struct JSContext* ctx);
// Persistent sampler for the per-frame tick. A tick that runs for a minute is
// indistinguishable from a dead page unless something outside it reports where
// the JS thread is parked, and spawning a thread per tick is not an option at
// 60 frames per second.
static DWORD WINAPI nfu_tick_watchdog(LPVOID) {
    uint32_t last = 0, last_rip = 0;
    for (;;) {
        RunCtx* rc = (RunCtx*)g_tick_rc;
        uint32_t now = platform_tick_ms();
        if (rc && now - last > 2000) {
            last = now;
            fprintf(stderr, "\njs: TWD last_c=%s t=%u now=%u\n",
                    rc->last_c ? (const char*)rc->last_c : "?", rc->last_c_t, now);
            fflush(stderr);
        }
        // A stalled tick parks the JS thread inside some native routine and
        // the interrupt handler (which only runs between bytecode ops) never
        // fires, so name the exact instruction instead of guessing: sample the
        // instruction pointer of the JS thread and print it as a module offset
        // that addr2line can resolve against the built binary.
        if (rc && rc->jtid && rc->start_t && now - rc->start_t > 3000 && now - last_rip > 4000) {
            last_rip = now;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                   FALSE, rc->jtid);
            if (th) {
                if (SuspendThread(th) != (DWORD)-1) {
                    CONTEXT ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(th, &ctx)) {
#if defined(_M_X64) || defined(__x86_64__)
                        unsigned long long rip = (unsigned long long)ctx.Rip;
#else
                        unsigned long long rip = (unsigned long long)ctx.Eip;
#endif
                        fprintf(stderr, "\njs: TRIP off=+0x%llx last_c=%s stuck=%ums js=%s line=%d\n",
                                rip - rc->exe_base,
                                rc->last_c ? (const char*)rc->last_c : "?",
                                (unsigned)(now - rc->start_t),
                                nfu_js_dbg_name(), nfu_js_dbg_line((JSContext*)s_ctx));
                        fflush(stderr);
                    }
                    ResumeThread(th);
                }
                CloseHandle(th);
            }
        }
        Sleep(250);
    }
    return 0;
}
#endif

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

// Remember which URL the next script runs from: dynamic import() resolves
// relative chunk specifiers against it (see nfu_mod_normalize).
void mini_js_set_module_base(const char* url) {
    nfu_mod_set_base(url);
}

// One-shot wall-clock budget for the NEXT mini_js_run call (milliseconds).
// 0 = use the default / NEFU_JS_DEADLINE env. Lets the browser bound phases
// whose page code may block synchronously (DOMContentLoaded handlers that
// fire social-API fetches which can each stall ~19 s in WinInet).
static volatile uint32_t g_run_deadline_ms = 0;

void mini_js_set_run_deadline(unsigned int ms) { g_run_deadline_ms = ms; }

int mini_js_run(const char* script, char* out, int out_sz,
                    char* html, int html_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    // debug: dump every script fed to the engine (NF_DUMP_JS=<dir>)
    {
        const char* ddir = getenv("NF_DUMP_JS");
        if (ddir && *ddir) {
            static int dn = 0;
            char dp[320];
            int dn2 = dn++;
            snprintf(dp, sizeof(dp), "%s\\js_%03d.js", ddir, dn2);
            FILE* df = fopen(dp, "wb");
            if (df) { fwrite(script, 1, strlen(script), df); fclose(df); }
            fprintf(stderr, "jsdump: %s len=%zu\n", dp, strlen(script)); fflush(stderr);
        }
    }
    static int veh_once = 0;
    if (!veh_once) {
        veh_once = 1;
#ifndef NEFU_BARE
        AddVectoredExceptionHandler(1, nfu_veh);
#endif
        // deliberate crash hook: NF_CRASH_TEST=1 -> NULL deref to verify the
        // VEH writes %TEMP%\nefu_js_crash.txt (diagnostic aid only)
        if (getenv("NF_CRASH_TEST")) {
            *(volatile int*)0 = 1;
        }
    }
    out[0] = 0;
    if (s_mem_dead) return 1;   // fuse tripped: this page's JS stays dead
    js_lock();
    ensure_js();
    if (!s_ctx) { js_unlock(); return 1; }   // never leak the spinlock
    // refresh the stack baseline for this thread: quickjs captured stack_top
    // on the thread that created the runtime; if evals now run on a worker
    // thread the stale baseline makes the overflow check meaningless.
    JS_UpdateStackTop(s_rt);
#ifndef NEFU_BARE
    if (js_trace_enabled()) { fprintf(stderr, "js: run enter len=%d ctx=%p tid=%lu\n",
            (int)strlen(script), (void*)s_ctx, (unsigned long)GetCurrentThreadId()); fflush(stderr); }
#endif
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    rc.html = html; rc.html_sz = html_sz; rc.hi = 0;
    if (html && html_sz > 0) html[0] = 0;
    const char* dl_env = getenv("NEFU_JS_DEADLINE");
    // Single-eval budget. 700 s let one runaway page script own the eval slot
    // for minutes (bloxd's lobby script never returned, every queued click
    // stayed parked). 20 s proved too tight: bloxd's 3.5 MB bundle needs its
    // bootstrap eval to finish in one piece, and cutting it mid-way leaves a
    // half-built engine. 60 s keeps real bootstraps intact; a genuinely stuck
    // script is still cut, and a parked click can cut it sooner through
    // mini_js_request_preempt().
    uint32_t run_budget = (dl_env && *dl_env) ? (uint32_t)strtol(dl_env, 0, 10) : 60000u;
    uint32_t one_shot = g_run_deadline_ms;
    g_run_deadline_ms = 0;
    if (one_shot) run_budget = one_shot;   // explicit per-call budget wins
    rc.deadline = platform_tick_ms() + run_budget;   // script budget (bloxd engine init + React mount)
    rc.hard_deadline = 0;                            // no grace on the boot script
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
            // NOTE: `href` is an accessor on the shim's location object, and its
            // setter requests a navigation. Writing it here (this runs before
            // every eval) would make the tab reload itself forever, so the
            // backing field is written directly instead.
            JS_SetPropertyStr(s_ctx, loc2, "__href", JS_NewString(s_ctx, s_env_url));
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
            // pathname/search/hash, so a router that reads them on boot sees the
            // real address instead of the shim's defaults
            {
                char pn[512]; int pnn = 0;
                char qs[256]; int qsn = 0;
                char hh[128]; int hhn = 0;
                const char* p = h2 ? h2 : "/";
                for (; *p && *p != '?' && *p != '#' && pnn < (int)sizeof(pn) - 1; p++) pn[pnn++] = *p;
                pn[pnn] = 0;
                if (*p == '?') { qs[qsn++] = *p++; for (; *p && *p != '#' && qsn < (int)sizeof(qs) - 1; p++) qs[qsn++] = *p; }
                qs[qsn] = 0;
                if (*p == '#') { for (; *p && hhn < (int)sizeof(hh) - 1; p++) hh[hhn++] = *p; }
                hh[hhn] = 0;
                if (!pn[0]) { pn[0] = '/'; pn[1] = 0; }
                JS_SetPropertyStr(s_ctx, loc2, "pathname", JS_NewString(s_ctx, pn));
                JS_SetPropertyStr(s_ctx, loc2, "search", JS_NewString(s_ctx, qs));
                JS_SetPropertyStr(s_ctx, loc2, "hash", JS_NewString(s_ctx, hh));
                {
                    char org[192]; int on = 0;
                    for (const char* t = prot; *t && on < (int)sizeof(org) - 4; t++) org[on++] = *t;
                    org[on++] = '/'; org[on++] = '/';
                    for (const char* t = hnb; *t && on < (int)sizeof(org) - 1; t++) org[on++] = *t;
                    org[on] = 0;
                    JS_SetPropertyStr(s_ctx, loc2, "origin", JS_NewString(s_ctx, org));
                }
            }
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
    if (js_trace_enabled()) fprintf(stderr, "js: run %d bytes\n", (int)strlen(script));
    uint32_t eval_t0 = platform_tick_ms();
    size_t script_len = strlen(script);
    if (script_len > 8) {
        unsigned char e0 = (unsigned char)script[script_len - 1];
        unsigned char e1 = (unsigned char)script[script_len];
        if (js_trace_enabled()) { fprintf(stderr, "js: script tail byte=%02x nul=%02x\n", e0, e1); fflush(stderr); }
    }
    const char* presrc = "try { var __si = 0, __to = 0; setInterval(function(){ __si = 7; }, 10); setTimeout(function(){ __to = 9; }, 10); 'SI:' + __si + '|TO:' + __to + '|ATOB:' + atob('RXJyb3I='); } catch(x) { 'WERRX:' + String(x && x.message); }";
    JSValue prv = JS_Eval(s_ctx, presrc, (size_t)strlen(presrc), "<pre>", JS_EVAL_TYPE_GLOBAL);
    if (!JS_IsException(prv)) {
        const char* pm = JS_ToCString(s_ctx, prv);
        if (pm && js_trace_enabled()) { fprintf(stderr, "js: pre: %s\n", pm); JS_FreeCString(s_ctx, pm); }
    } else { JSValue pe = JS_GetException(s_ctx); JS_FreeValue(s_ctx, pe); }
    JS_FreeValue(s_ctx, prv);
#ifndef NEFU_BARE
    if (js_trace_enabled()) fprintf(stderr, "js: eval-start t=%u tid=%lu\n", platform_tick_ms(),
                                    (unsigned long)GetCurrentThreadId());
#endif
#ifdef NEFU_BARE
    klogf("js: eval-start len=%d t=%u\n", (int)strlen(script), platform_tick_ms());
#endif
    JSValue r = JS_Eval(s_ctx, script, script_len, "<script>",
                        JS_EVAL_TYPE_GLOBAL);
    {
        // A slow eval is the one thing that can stall the whole page: it owns
        // the eval slot, so input/clicks stay parked until it returns. Log
        // anything past 2 s so a stuck page names itself instead of being
        // diagnosed by staring at "ev slot busy" forever.
        uint32_t el = platform_tick_ms() - eval_t0;
        if (el > 2000) {
            klogf("js: slow eval %u ms len=%d exc=%d", el, (int)script_len,
                  JS_HasException(s_ctx) ? 1 : 0);
        }
    }
#ifdef NEFU_BARE
    klogf("js: eval-done t=%u exc=%d\n", platform_tick_ms(),
          JS_HasException(s_ctx) ? 1 : 0);
#endif
    // drain promise microtasks: React/Vue schedule work via setTimeout which our
    // shim maps to Promise.then; without JS_RunJob those callbacks never run and
    // the rendered DOM never reaches nativeSync.
    {
        int drain_guard = 0;
        int exc_printed = 0;
        uint32_t drain_start = platform_tick_ms();
        // preserve a pending eval exception across the drain: running jobs
        // with an exception pending is illegal in quickjs — the first
        // try/catch (or job bookkeeping) inside a job swallows or mangles
        // the stale exception, and JS_GetException later returns the
        // UNINITIALIZED marker (tag 4) instead of the real error
        JSValue saved_exc = JS_UNDEFINED;
        int had_exc = 0;
        if (JS_HasException(s_ctx)) { saved_exc = JS_GetException(s_ctx); had_exc = 1; }
        // drain regardless of eval exception AND regardless of the budget
        // deadline; bounded by count + wall-clock so a runaway render loop
        // (React re-scheduling itself forever) cannot freeze the host.
        // promise microtask drain re-enabled with tight bounds: React 18
        // schedules its render commits through promise/microtask chains, and
        // with the drain disabled the menu DOM never materializes (js_html
        // stays empty). bellard's JS_ExecutePendingJob has no interrupt check,
        // so bound by count and wall-clock; a runaway job loop cannot freeze
        // the host beyond the 8s window.
        {
            JSContext* job_ctx = NULL;
            while (JS_IsJobPending(s_rt) && drain_guard < 200000 &&
                   (platform_tick_ms() - drain_start) < 8000) {
                int jr = JS_ExecutePendingJob(s_rt, &job_ctx);
                if (jr < 0 && job_ctx) {
                    JSValue jx = JS_GetException(job_ctx);
                    if (!exc_printed) {
                        const char* jm = JS_ToCString(job_ctx, jx);
                        fprintf(stderr, "js: job exc: %s\n", jm ? jm : "?");
                        if (jm) JS_FreeCString(job_ctx, jm);
                        exc_printed = 1;
                    }
                    JS_FreeValue(job_ctx, jx);
                }
                drain_guard++;
            }
        }
        if (js_trace_enabled())
            fprintf(stderr, "js: drain done n=%d pending=%d\n", drain_guard,
                    JS_IsJobPending(s_rt) ? 1 : 0);
        if (had_exc) JS_Throw(s_ctx, saved_exc);   // restore the eval exception
        // run queued setTimeout callbacks (bounded, FIFO). bloxd's inline
        // deobfuscator defers Xj assignment into a setTimeout; the main bundle
        // (next eval) needs Xj to exist, so flush timers after every eval.
        {
            uint32_t ts_start = platform_tick_ms();
            // Wall-clock bound INSIDE the loop: a timer that issues a
            // synchronous HTTP request (bloxd's click handlers chain into
            // analytics/social fetches) cannot be interrupted by the eval
            // deadline, so an unbounded inline flush turned one click into
            // 40 s of dead UI. 2.5 s inline; the rest runs on later ticks.
            const char* tsrc =
                "try { var __nft = 0; var __t0 = Date.now(); while (globalThis.__nfuTimers && globalThis.__nfuTimers.length && __nft < 4000 && Date.now() - __t0 < 2500) { var __it = globalThis.__nfuTimers.shift(); __nft++; var __ff = (typeof __it === 'function') ? __it : (__it && __it.f); if (typeof __ff === 'function') { try { __ff(); } catch(e) {} } } "
                "'T:' + __nft + '|XJ:' + (typeof Xj) + '|SO:' + (typeof setTimeout) + '|W:' + (typeof window) + '|D:' + (typeof document) + '|GL:' + (typeof WebGLRenderingContext) + '|HCE:' + (typeof HTMLCanvasElement) + '|UA:' + ((navigator&&navigator.userAgent)?navigator.userAgent.substring(0,16):'?') + '|DPR:' + ((window&&window.devicePixelRatio)?window.devicePixelRatio:'?'); } catch(x) { 'TERR:' + String(x && x.message); }";
            JSValue tv = JS_Eval(s_ctx, tsrc, (size_t)strlen(tsrc), "<timers>", JS_EVAL_TYPE_GLOBAL);
            if (!JS_IsException(tv)) {
                const char* tm = JS_ToCString(s_ctx, tv);
                if (tm) { if (js_trace_enabled()) fprintf(stderr, "js: timers: %s\n", tm); JS_FreeCString(s_ctx, tm); }
            } else { JSValue te = JS_GetException(s_ctx); JS_FreeValue(s_ctx, te); }
            JS_FreeValue(s_ctx, tv);
        }
        // post-drain DOM dump: did React produce menu markup?
        const char* dmp = "try { var __bd = document.body.innerHTML; var __si = __bd.indexOf('script'); var __hd = __bd.substring(0, 160); 'BODYLEN=' + __bd.length + ' ROOTLEN=' + ((document.getElementById('root')||{}).innerHTML || '').length; } catch(x) { 'DUMPEXC:' + String(x && x.message); }";
        JSValue dv = JS_Eval(s_ctx, dmp, (size_t)strlen(dmp), "<dump>", JS_EVAL_TYPE_GLOBAL);
        if (!JS_IsException(dv)) {
            const char* dm = JS_ToCString(s_ctx, dv);
            if (dm) { if (js_trace_enabled()) fprintf(stderr, "js: %s\n", dm); JS_FreeCString(s_ctx, dm); }
        } else { JSValue de = JS_GetException(s_ctx); JS_FreeValue(s_ctx, de); }
        JS_FreeValue(s_ctx, dv);
        // force-commit the latest DOM: engine mutations (e.g. innerHTML writes
        // that replaced the preload loader with the VAPE menu) may have bypassed
        // dirty(), so push the live body markup into the C-side rc->html buffer.
        // Give the flush its own budget: the main eval's 300s deadline has long
        // expired by now and would otherwise interrupt the serialization itself.
        uint32_t saved_deadline = rc.deadline;
        rc.deadline = platform_tick_ms() + 5000;
        const char* fs2 = "try { __nfuSync(document.body.innerHTML); } catch(x) { 0; }";
        JSValue fv = JS_Eval(s_ctx, fs2, (size_t)strlen(fs2), "<flush>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(s_ctx, fv);
        rc.deadline = saved_deadline;
    }
    if (js_trace_enabled()) fprintf(stderr, "js: run done\n");
    rc.watch_done = 1;
#ifndef NEFU_BARE
    if (wd) { WaitForSingleObject(wd, 2000); CloseHandle(wd); }
#endif
    int err = 0;
    if (JS_IsException(r)) {
        err = 1;
        if (js_trace_enabled()) { fprintf(stderr, "js: S exc haspending=%d\n", JS_HasException(s_ctx)); fflush(stderr); }
        JSValue ex = JS_GetException(s_ctx);
        // identify the thrown value purely in C (this quickjs has no JS_TypeOf)
        {
            int tag = JS_VALUE_GET_TAG(ex);
            int isfn = JS_IsFunction(s_ctx, ex);
            fprintf(stderr, "js: exc tag=%d undef=%d null=%d str=%d num=%d bool=%d obj=%d fn=%d\n",
                    tag, JS_IsUndefined(ex), JS_IsNull(ex), JS_IsString(ex),
                    JS_IsNumber(ex), JS_IsBool(ex) ? 1 : 0, JS_IsObject(ex), isfn);
            fflush(stderr);
        }
        // JS_ToCString may itself fail (Symbol throws) — clear the spill
        const char* msg = JS_ToCString(s_ctx, ex);
        if (JS_HasException(s_ctx)) JS_GetException(s_ctx);
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
        // Diagnostic dumps are opt-in: they only exist to inspect exotic
        // exception objects and must stay off the hot path in normal runs.
        if (getenv("NF_JS_DIAG2")) {
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
        }
        // Re-running a page symbol with a nested JS_Eval inside the exception path
        // corrupts the runtime (it crashed inside free_bytecode_atoms on bloxd.io),
        // so the probe is opt-in and never on by default.
        if (getenv("NF_JS_DIAG")) {
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
    }
    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    js_unlock();
    return err;
}

void mini_js_reset(void) {
    js_lock();
    if (js_trace_enabled()) { fprintf(stderr, "js: reset (ctx=%p)\n", (void*)s_ctx); fflush(stderr); }
    if (s_ctx) { JS_FreeContext(s_ctx); s_ctx = 0; }
    if (s_rt) { JS_FreeRuntime(s_rt); s_rt = 0; }
    s_mem_dead = 0;   // new page gets a fresh fuse
    // The seeded DOM belongs to the page that is going away. Keeping it would
    // let a script that runs before the next page seeds its tree start from
    // the previous document's markup.
    s_env_dom = 0;
    js_unlock();
}

bool mini_js_alive(void) {
    return s_ctx != 0;
}

// Per-frame pump. The browser calls this once per frame from the thread that
// owns the runtime (the resident JS worker), so the page's globals stay valid
// across frames. It runs the queued requestAnimationFrame callbacks, the
// timers whose due time has passed, the armed intervals, then drains promise
// microtasks; the DOM shim re-serializes itself through __nfuSync whenever a
// listener mutated the tree.
// A 250 ms budget used to keep a runaway render loop from stalling the host,
// but a real game page resolves one heavy promise (identity fetch) and then
// runs React re-render + Turnstile + ad adaptors in the SAME frame: the
// interrupt fired mid-chain and QuickJS reports "InternalError: interrupted",
// which aborts bloxd's whole initialisation (no canvas, no matchmaking). The
// budget is therefore per-frame but generous; the per-tick callback caps below
// (rAF 64 / timers 192) still bound a runaway loop.
static const uint32_t kFrameBudgetMs = 6000;
// Ceiling for one tick's grace extension (see js_interrupt_cb): long native
// work may overrun the frame budget, a runaway page still gets cut.
static const uint32_t kTickHardCapMs  = 120000;
// The anti-bot deobfuscation wrapper bloxd serves inline can need minutes of
// uninterrupted JS before it emits the real bootstrap script. Cutting it at the
// tick ceiling aborts the wrapper silently and the page then sits on the loader
// forever with zero network activity, so the ceiling is overridable:
//   NEFU_TICK_HARD_MS=<ms>
static uint32_t tick_hard_cap_ms(void) {
    static uint32_t s_cap = 0;
    if (!s_cap) {
        const char* v = getenv("NEFU_TICK_HARD_MS");
        s_cap = kTickHardCapMs;
        if (v && *v) {
            int n = atoi(v);
            if (n >= 1000 && n <= 1800000) s_cap = (uint32_t)n;
        }
        fprintf(stderr, "\n[TICKCAP] hard=%u ms\n", (unsigned)s_cap);
        fflush(stderr);
    }
    return s_cap;
}
// Returns the number of callbacks executed (0 = nothing was pending).
int mini_js_tick(char* out, int out_sz, char* html, int html_sz) {
    if (!out || out_sz <= 1) return 0;
    out[0] = 0;
    {   // low-frequency entry trace: proves whether the runtime pump keeps
        // being scheduled after the page settles
        static uint32_t last_tent = 0;
        uint32_t nt = platform_tick_ms();
        if (js_trace_enabled() && nt - last_tent > 5000) { last_tent = nt; fprintf(stderr, "js: TICK enter\n"); }
    }
    if (s_mem_dead) return 0;   // fuse tripped: this page's JS stays dead
    if (!s_ctx) return 0;
    // Where the frame time goes. A tick that never returns is indistinguishable
    // from a page that simply has nothing to do, and the browser only sees
    // "request outstanding", so each stage reports its own cost and the entry
    // line carries the previous tick's total.
    static uint32_t s_tick_no = 0;
    static uint32_t s_tick_prev = 0;
    static uint32_t s_tick_prev_dt = 0;
    uint32_t t_tick0 = platform_tick_ms();
    int wnoise = (s_tick_no % 30 == 1) || (s_tick_prev_dt > 300);
    if (wnoise) {
        fprintf(stderr, "js: TIN n=%u prev_dt=%u\n", (unsigned)s_tick_no, (unsigned)s_tick_prev_dt);
        fflush(stderr);
    }
    s_tick_no++;
    s_tick_prev = platform_tick_ms();
    js_lock();
    if (!s_ctx) { js_unlock(); return 0; }
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    rc.html = html; rc.html_sz = html_sz; rc.hi = 0;
    if (html && html_sz > 0) html[0] = 0;
    rc.deadline = platform_tick_ms() + kFrameBudgetMs;
    // A tick may run long exactly once (model decoding, module instantiation)
    // but must never run away: past this ceiling the interrupt stands.
    rc.hard_deadline = rc.deadline + tick_hard_cap_ms();
    rc.last_c = 0; rc.last_c_t = 0; rc.watch_done = 1;
#ifndef NEFU_BARE
    rc.jtid = GetCurrentThreadId();
    rc.exe_base = (unsigned long long)GetModuleHandleA(NULL);
#else
    rc.jtid = 0;
    rc.exe_base = 0;
#endif
    rc.rip_printed = 0; rc.rip_t = 0;
#ifndef NEFU_BARE
    {
        // Pointer to the tick context, read by the persistent sampler above.
        static HANDLE s_twd2 = 0;
        if (!s_twd2) s_twd2 = CreateThread(NULL, 0, nfu_tick_watchdog, 0, 0, NULL);
        g_tick_rc = &rc;
        rc.start_t = platform_tick_ms();
    }
#endif
    JS_SetInterruptHandler(JS_GetRuntime(s_ctx), js_interrupt_cb, &rc);
    JS_SetContextOpaque(s_ctx, &rc);
    {   // one-shot engine state probe (stderr side-channel): timer queue,
        // interval queue, clock bases — settles which clock the pump sees.
        // Sampling starts 15 s after the first tick so page boot (seg evals,
        // api.js render) has settled and late-created queues are visible.
        static int dbg_n = 0;
        static uint32_t dbg_t0 = 0;
        if (!dbg_t0) dbg_t0 = platform_tick_ms();
        if (js_trace_enabled() && dbg_n < 8 && platform_tick_ms() - dbg_t0 > 15000) {
            static uint32_t dbg_last = 0;
            uint32_t dnow_ms = platform_tick_ms();
            if (dnow_ms - dbg_last >= 2000) {
            dbg_n++;
            dbg_last = dnow_ms;
            static const char* dbg_src =
                "(function(){"
                " var t = globalThis.__nfuTimers, iv = globalThis.__nfuIntervals;"
                " var pn = (globalThis.performance && performance.now) ? performance.now() : -1;"
                " return 'pnow=' + pn"
                " + ' tms=' + (t ? t.length : -1)"
                " + ' t0type=' + (t && t[0] ? (typeof t[0]) : 'none')"
                " + ' t0due=' + (t && t[0] && typeof t[0] === 'object' ? t[0].due : (t && t[0] ? 'fn' : 'none'))"
                " + ' t0src=' + (t && t[0] ? String(typeof t[0] === 'function' ? t[0] : t[0].f).slice(0, 90) : 'none')"
                " + ' ivs=' + (iv ? iv.length : -1)"
                " + ' iv0src=' + (iv && iv[0] ? String(iv[0].f).slice(0, 60) : 'none')"
                " + ' si=' + typeof globalThis.setInterval"
                " + ' probeBare=' + (typeof __nfuProbe)"
                " + ' ivBare=' + (typeof __nfuIntervals)"
                " + ' ivWin=' + (globalThis.window && globalThis.window.__nfuIntervals ? globalThis.window.__nfuIntervals.length : 'none')"
                " + ' ivTms=' + (globalThis.window && globalThis.window.__nfuTimers ? globalThis.window.__nfuTimers.length : 'none')"
                " + ' winEq=' + (globalThis.window === globalThis)"
                " + ' probeSet=' + globalThis.__nfuProbeSet"
                " + ' probeSet2=' + String(globalThis.__nfuProbeSet2).slice(0, 60)"
                " + ' nfCalls=' + (globalThis.__nfuNfCalls|0)"
                " + ' fired=' + (globalThis.__nfuPollFired|0)"
                " + ' info=' + String(globalThis.__nfuPollInfo).slice(0, 80)"
                " + ' dnow=' + Date.now(); })()";
            JSValue dv = JS_Eval(s_ctx, dbg_src, strlen(dbg_src), "<pumpdbg>", 0);
            if (!JS_IsException(dv)) {
                const char* ds = JS_ToCString(s_ctx, dv);
                fprintf(stderr, "js: PUMPDBG %s\n", ds ? ds : "?");
                if (ds) JS_FreeCString(s_ctx, ds);
            } else {
                JSValue ex = JS_GetException(s_ctx);
                const char* em = JS_ToCString(s_ctx, ex);
                fprintf(stderr, "js: PUMPDBG-EXC %s\n", em ? em : "?");
                if (em) JS_FreeCString(s_ctx, em);
                JS_FreeValue(s_ctx, ex);
            }
            JS_FreeValue(s_ctx, dv);
            fflush(stderr);
            }
        }
    }

    // The queues are detached (swapped out) before running, so callbacks that
    // re-schedule themselves land in the fresh queue for the next frame
    // instead of being executed again in this one.
    static const char* const pump_src =
        "(function(){"
        // deliver queued WebSocket messages before anything else in the frame
        " if (globalThis.__nfuWsPump){ try { globalThis.__nfuWsPump(); } catch(e){} }"
        " var now = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();"
        " var n = 0, i;"
        " var q = globalThis.__nfuRaf; globalThis.__nfuRaf = [];"
        " globalThis.__nfuPumpN = (globalThis.__nfuPumpN || 0) + 1;"
        " if (globalThis.__nfuPumpN % 200 === 1) { try { print('[PUMP] n=' + globalThis.__nfuPumpN + ' raf=' + (q ? q.length : 0) + ' tm=' + ((globalThis.__nfuTimers||[]).length) + ' iv=' + ((globalThis.__nfuIntervals||[]).length)); } catch(e){} }"
        " if (q) for (i = 0; i < q.length && i < 64; i++){ try { q[i].f(now); } catch(e){ globalThis.__nfuRafErrs = (globalThis.__nfuRafErrs||0)+1; if (globalThis.__nfuRafErrs < 24) { try { var _rn = (e && e.name ? e.name : 'Err'); var _rm = (e && e.message ? e.message : ''); print('[RAF-ERR] ' + _rn + ': ' + _rm + ' | ' + (e && e.stack ? e.stack : e)); } catch(e2){} } } n++; }"
        " var tm = globalThis.__nfuTimers;"
        " if (tm && tm.length){"
                "   var keep = [];"
        "   for (i = 0; i < tm.length; i++){"
        "     var t = tm[i];"
        "     var f = t ? ((typeof t === 'function') ? t : t.f) : null;"
        "     if (typeof f !== 'function') continue;"
        "     if ((typeof t === 'function' || t.due <= now) && n < 192){ try { if (t && t.a && t.a.length) { f.apply(undefined, t.a); } else { f(); } } catch(e){ globalThis.__nfuTimerErrs = (globalThis.__nfuTimerErrs||0)+1; if (globalThis.__nfuTimerErrs < 30) { try { var _nm = (e && e.name ? e.name : 'Err'); var _ms = (e && e.message ? e.message : ''); print('[TIMER-ERR] ' + _nm + ': ' + _ms + ' | ' + (e && e.stack ? e.stack : e)); } catch(e2){} } } }"
        "     else { keep.push(t); if ((globalThis.__nfuTmoDbg|0) < 24){ globalThis.__nfuTmoDbg = (globalThis.__nfuTmoDbg|0)+1; try { print('[TMO-SKIP] due-now=' + (t.due - now) + ' pnow=' + now + ' datenow=' + Date.now()); } catch(e3){} } }"
        "   }"
        "   globalThis.__nfuTimers = keep;"
        " }"
        // Worker messages are drained on their own budget: geometry work arrives
        // as main->worker posts and the mesh only appears once the answers come
        // back, so this must never be starved by the timer list.
        " try { if (globalThis.__nfuWDrain) { var __wd = globalThis.__nfuWDrain(48); if (__wd && (globalThis.__nfuWdLog = (globalThis.__nfuWdLog||0)+1) <= 6) { try { print('[WQDRAIN] n=' + __wd + ' left=' + ((globalThis.__nfuWq||[]).length)); } catch(e0){} } } } catch(e1){}"
        " var iv = globalThis.__nfuIntervals;"
        " if (iv) for (i = 0; i < iv.length && n < 192; i++){"
        "   var v = iv[i];"
        "   if (v && typeof v.f === 'function' && v.due <= now){ v.due = now + v.every; try { if (v.a && v.a.length) { v.f.apply(undefined, v.a); } else { v.f(); } } catch(e){} n++; }"
        " }"
        // the shim only flags mutations; one serialization per frame, and a
        // microtask-only mutation still counts as work so the UI re-renders.
        // The serialize itself moved out of this eval (below): building the
        // 100 KB+ innerHTML of a framework page can exceed the frame budget,
        // and an interrupted serialize used to be lost forever because the
        // dirty flag was cleared before the attempt.
        " var fl = 0;"
        " if (globalThis.__nfuDirty){ globalThis.__nfuDirty = 0; globalThis.__nfuNeedSync = 1; fl = 1; }"
        " return n + fl;"
        "})()";
    JSValue r = JS_Eval(s_ctx, pump_src, (size_t)strlen(pump_src), "<tick>",
                        JS_EVAL_TYPE_GLOBAL);
    int ran = 0;
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(s_ctx);
        const char* em = JS_ToCString(s_ctx, ex);
        fprintf(stderr, "js: PUMP-EXC %s\n", em ? em : "?");
        if (em) JS_FreeCString(s_ctx, em);
        JS_FreeValue(s_ctx, ex);
    } else {
        int32_t v = 0;
        if (JS_ToInt32(s_ctx, &v, r) == 0) ran = (int)v;
    }
    JS_FreeValue(s_ctx, r);
    {
        uint32_t el = platform_tick_ms() - t_tick0;
        if (el > 300 || wnoise) {
            fprintf(stderr, "js: TPUMP %ums\n", (unsigned)el);
            fflush(stderr);
        }
    }
    // DOM flush with its own, generous budget (see the pump comment above):
    // building framework pages' full innerHTML easily exceeds a frame, and
    // unlike the old inline attempt a failure here re-arms the flag so the
    // next frame retries instead of the update being lost forever.
    {
        JSValue nv = JS_Eval(s_ctx, "(globalThis.__nfuNeedSync === 1) ? 1 : 0",
                             (size_t)strlen("(globalThis.__nfuNeedSync === 1) ? 1 : 0"),
                             "<syncq>", 0);
        int need = 0;
        if (!JS_IsException(nv)) JS_ToInt32(s_ctx, &need, nv);
        JS_FreeValue(s_ctx, nv);
        if (need == 1) {
            uint32_t saved_dl = rc.deadline;
            rc.deadline = platform_tick_ms() + 3000;
            static const char* const sync_src =
                "globalThis.__nfuNeedSync = 0;"
                " try { __nfuSync(document.body.innerHTML); }"
                " catch(e){ globalThis.__nfuNeedSync = 1; }";
            JSValue sv = JS_Eval(s_ctx, sync_src, (size_t)strlen(sync_src),
                                 "<sync>", 0);
            if (JS_IsException(sv)) {
                JSValue ex2 = JS_GetException(s_ctx);
                JS_FreeValue(s_ctx, ex2);
                rc.deadline = platform_tick_ms() + 3000;   // fresh try next frame
                static const char* const rearm = "globalThis.__nfuNeedSync = 1";
                JS_Eval(s_ctx, rearm, (size_t)strlen(rearm), "<syncr>", 0);
            }
            JS_FreeValue(s_ctx, sv);
            rc.deadline = saved_dl;
        }
    }
    // promise microtasks (React's scheduler, fetch continuations, async/await)
    {
        uint32_t el = platform_tick_ms() - t_tick0;
        if (el > 300 || wnoise) {
            fprintf(stderr, "js: TSYNC %ums\n", (unsigned)el);
            fflush(stderr);
        }
    }
    {
        JSContext* jc = NULL;
        int guard = 0;
        uint32_t ds = platform_tick_ms();
        while (JS_IsJobPending(s_rt) && guard < 2000 &&
               (platform_tick_ms() - ds) < 200) {
            JS_ExecutePendingJob(s_rt, &jc);
            guard++;
        }
        // heartbeat: shows whether an async chain (e.g. the Turnstile
        // challenge VM) is alive-slow vs fully stalled
        static uint32_t last_hb = 0;
        uint32_t now_hb = platform_tick_ms();
        if (now_hb - last_hb > 3000) {
            last_hb = now_hb;
            int jobs = 0;
            // Same wall-clock cap as the frame pump: a promise chain that
            // re-queues a job on every continuation (bloxd's room handler does
            // this while it drains room state) otherwise keeps this loop here
            // forever, and the whole page stops painting.
            uint32_t js0 = platform_tick_ms();
            while (JS_IsJobPending(s_rt) && jobs < 100000 && (platform_tick_ms() - js0) < 300) { JS_ExecutePendingJob(s_rt, &jc); jobs++; }
            if (jobs >= 40) {
                static int s_jobcap_n = 0;
                if (s_jobcap_n++ < 12) {
                    fprintf(stderr, "js: JOBCAP jobs=%d ms=%u\n", jobs,
                            (unsigned)(platform_tick_ms() - js0));
                    fflush(stderr);
                }
            }
            int tms = 0;
            JSValue tc = JS_Eval(s_ctx, "(globalThis.__nfuTimers ? globalThis.__nfuTimers.length : 0)",
                                 (size_t)strlen("(globalThis.__nfuTimers ? globalThis.__nfuTimers.length : 0)"), "<hb>", 0);
            if (!JS_IsException(tc)) { int64_t v = 0; JS_ToInt64(s_ctx, &v, tc); tms = (int)v; }
            JS_FreeValue(s_ctx, tc);
            if (jobs > 0 || tms > 0 || s_mem_dead)
                fprintf(stderr, "js: HB jobs=%d timers=%d dead=%d ctx=%d\n", jobs, tms,
                        (int)s_mem_dead, s_ctx ? 1 : 0);
        }
    }
    out[rc.oi] = 0;
    {
        uint32_t el = platform_tick_ms() - t_tick0;
        s_tick_prev_dt = el;
        if (el > 300 || wnoise) {
            fprintf(stderr, "js: TDONE n=%u tot=%ums\n", (unsigned)(s_tick_no - 1), (unsigned)el);
            fflush(stderr);
        }
    }
    JS_SetInterruptHandler(JS_GetRuntime(s_ctx), 0, 0);
#ifndef NEFU_BARE
    g_tick_rc = 0;
#endif
    js_unlock();
    return ran;
}

// Deliver one DOM event (click / mousedown / mousemove / mouseup / keydown /
// keyup / wheel / input) to the page. `idx` is the data-nfu index of the
// hit-tested element (-1 = document/window only), `key` a keysym string or
// keycode, `mods` the modifier bitmask (1 shift, 2 ctrl, 4 alt, 8 meta).
int mini_js_dispatch(const char* type, int idx, int x, int y,
                     const char* key, int mods) {
    if (!s_ctx || !type || !type[0]) return 0;
    char src[256];
    char kbuf[32] = "";
    if (key && key[0]) {
        // single char / keysym name: pass as a quoted string, otherwise a code
        int n = 0;
        kbuf[n++] = '\'';
        for (const char* p = key; *p && n < (int)sizeof(kbuf) - 3; p++) {
            if (*p == '\'' || *p == '\\') kbuf[n++] = '\\';
            kbuf[n++] = *p;
        }
        kbuf[n++] = '\'';
        kbuf[n] = 0;
    } else {
        strcpy(kbuf, "0");
    }
    snprintf(src, sizeof(src),
             "try { __nfuEvent('%s', %d, %d, %d, %s, %d, 0); } catch(e){}",
             type, idx, x, y, kbuf, mods);
    js_lock();
    if (!s_ctx) { js_unlock(); return 0; }
    RunCtx rc;
    char scratch[64];
    rc.out = scratch; rc.out_sz = (int)sizeof(scratch); rc.oi = 0;
    rc.html = 0; rc.html_sz = 0; rc.hi = 0;
    rc.deadline = platform_tick_ms() + 300;
    rc.hard_deadline = 0;
    rc.last_c = 0; rc.last_c_t = 0; rc.watch_done = 1;
#ifndef NEFU_BARE
    rc.jtid = GetCurrentThreadId();
    rc.exe_base = (unsigned long long)GetModuleHandleA(NULL);
#else
    rc.jtid = 0;
    rc.exe_base = 0;
#endif
    rc.rip_printed = 0; rc.rip_t = 0;
    JS_SetInterruptHandler(JS_GetRuntime(s_ctx), js_interrupt_cb, &rc);
    JS_SetContextOpaque(s_ctx, &rc);
    JSValue r = JS_Eval(s_ctx, src, (size_t)strlen(src), "<event>",
                        JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(s_ctx);
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, r);
    JS_SetInterruptHandler(JS_GetRuntime(s_ctx), 0, 0);
    js_unlock();
    return 0;
}

} // namespace nefu
#endif

