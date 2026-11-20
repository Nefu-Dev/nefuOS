/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// WebAssembly over wasm3 - see wasm_js.h for why this exists.
//
// Layout of the bridge:
//   * a tiny wasm binary reader for the three sections the JS side needs
//     (type, function, export) - it never touches code, only signatures;
//   * one M3Runtime per WebAssembly.Instance, so each instance owns its
//     linear memory exactly like a real browser instance would;
//   * the six wasm-bindgen glue imports are linked as raw calls and forwarded
//     to the JavaScript functions the caller passed in `imports`, keeping the
//     i32 handles opaque on the JS side as bloxd's glue expects;
//   * exports are wrapped per signature and call back through m3_Call.
#include "wasm_js.h"

// Provided by the JS host: pushes the current tick's interrupt deadline out so a
// long but legitimate wasm job is not cut off as "interrupted".
extern "C" void nfu_js_extend_deadline(uint32_t ms);

#include "../../third_party/wasm3/wasm3.h"
#include "../../third_party/wasm3/m3_env.h"  // full M3Runtime/M3Module layout

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace nefu {
namespace {

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------
static bool wasm_trace() {
    static int on = -1;
    if (on < 0) on = getenv("NF_WASM_TRACE") ? 1 : 0;
    return on >= 1;
}

// Stricter level: NF_WASM_TRACE=2 also logs every glue callback, which is how a
// wasm export that never returns can be traced down to the import it is stuck in.
static bool wasm_trace2() {
    static int on = -1;
    if (on < 0) {
        const char* v = getenv("NF_WASM_TRACE");
        on = (v && v[0] == '2') ? 2 : 0;
    }
    return on >= 2;
}

#define WLOG(...)                        \
    do {                                 \
        if (wasm_trace()) {              \
            fprintf(stderr, __VA_ARGS__); \
            fflush(stderr);              \
        }                                \
    } while (0)

// ---------------------------------------------------------------------------
// wasm binary signature reader
// ---------------------------------------------------------------------------
const int kMaxTypes = 512;
const int kMaxFuncs = 4096;
const int kMaxExports = 512;

struct SigInfo {
    int nparams;
    int nresults;
    uint8_t params[16];
    uint8_t results[4];
};

struct ModuleInfo {
    int ntypes;
    SigInfo types[kMaxTypes];
    int nimportedFuncs;
    int nfuncs;                    // defined functions
    uint32_t funcType[kMaxFuncs];  // type index per defined function
    int nexports;
    char expName[kMaxExports][48];
    uint8_t expKind[kMaxExports];
    uint32_t expIndex[kMaxExports];
    bool hasMemory;
    bool ok;
};

static uint32_t rd_leb(const uint8_t* b, size_t len, size_t* io) {
    uint32_t r = 0;
    int s = 0;
    while (*io < len) {
        uint8_t x = b[(*io)++];
        r |= (uint32_t)(x & 0x7f) << s;
        s += 7;
        if (!(x & 0x80)) break;
    }
    return r;
}

// Read `count` bytes of a name and copy it into out with a hard cap.
static void rd_name(const uint8_t* b, size_t len, size_t* io, char* out, int cap) {
    uint32_t n = rd_leb(b, len, io);
    int copy = (int)n < cap - 1 ? (int)n : cap - 1;
    if (*io + n > len) { *io = len; return; }
    memcpy(out, b + *io, (size_t)copy);
    out[copy] = 0;
    *io += n;
}

// Parse just enough of the module to drive the JS wrappers. Returns false when
// the stream is not a well-formed wasm module.
static bool parse_module_info(const uint8_t* b, size_t len, ModuleInfo* mi) {
    memset(mi, 0, sizeof(*mi));
    if (len < 8 || b[0] != 0x00 || b[1] != 'a' || b[2] != 's' || b[3] != 'm') return false;
    if (b[4] != 1 || b[5] != 0 || b[6] != 0 || b[7] != 0) return false;  // only 32-bit MVP
    size_t pos = 8;
    while (pos < len) {
        uint8_t sid = b[pos++];
        uint32_t sz = rd_leb(b, len, &pos);
        if (pos + sz > len) return false;
        size_t end = pos + sz;
        if (sid == 1) {  // type section
            uint32_t cnt = rd_leb(b, len, &pos);
            for (uint32_t i = 0; i < cnt; i++) {
                if (pos >= end) return false;
                if (b[pos++] != 0x60) return false;
                SigInfo* si = (mi->ntypes < kMaxTypes) ? &mi->types[mi->ntypes] : NULL;
                uint32_t np = rd_leb(b, len, &pos);
                for (uint32_t k = 0; k < np; k++) {
                    if (pos >= end) return false;
                    if (si && (int)k < 16) si->params[k] = b[pos];
                    pos++;
                }
                uint32_t nr = rd_leb(b, len, &pos);
                for (uint32_t k = 0; k < nr; k++) {
                    if (pos >= end) return false;
                    if (si && (int)k < 4) si->results[k] = b[pos];
                    pos++;
                }
                if (si) {
                    si->nparams = (int)(np < 16 ? np : 16);
                    si->nresults = (int)(nr < 4 ? nr : 4);
                    mi->ntypes++;
                }
            }
        } else if (sid == 2) {  // import section
            uint32_t cnt = rd_leb(b, len, &pos);
            for (uint32_t i = 0; i < cnt; i++) {
                char mod[64], nm[64];
                rd_name(b, end, &pos, mod, 64);
                rd_name(b, end, &pos, nm, 64);
                if (pos >= end) return false;
                uint8_t kind = b[pos++];
                if (kind == 0) {  // func
                    rd_leb(b, end, &pos);  // type index
                    mi->nimportedFuncs++;
                } else if (kind == 1) {  // table
                    pos++;               // elem type
                    uint32_t fl = rd_leb(b, end, &pos);
                    rd_leb(b, end, &pos);  // min
                    if (fl & 1) rd_leb(b, end, &pos);
                } else if (kind == 2) {  // memory
                    uint32_t fl = rd_leb(b, end, &pos);
                    rd_leb(b, end, &pos);  // min
                    if (fl & 1) rd_leb(b, end, &pos);
                } else if (kind == 3) {  // global
                    pos += 2;              // valtype + mut
                } else {
                    return false;
                }
            }
        } else if (sid == 3) {  // function section
            uint32_t cnt = rd_leb(b, len, &pos);
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t t = rd_leb(b, end, &pos);
                if (mi->nfuncs < kMaxFuncs) mi->funcType[mi->nfuncs++] = t;
            }
        } else if (sid == 5) {  // memory section
            uint32_t cnt = rd_leb(b, len, &pos);
            if (cnt > 0) mi->hasMemory = true;
        } else if (sid == 7) {  // export section
            uint32_t cnt = rd_leb(b, len, &pos);
            for (uint32_t i = 0; i < cnt; i++) {
                char nm[48];
                rd_name(b, end, &pos, nm, 48);
                if (pos >= end) return false;
                uint8_t kind = b[pos++];
                uint32_t idx = rd_leb(b, end, &pos);
                if (mi->nexports < kMaxExports) {
                    strcpy(mi->expName[mi->nexports], nm);
                    mi->expKind[mi->nexports] = kind;
                    mi->expIndex[mi->nexports] = idx;
                    mi->nexports++;
                }
            }
        }
        pos = end;
    }
    mi->ok = mi->ntypes > 0;
    return mi->ok;
}

// Resolve the signature of an exported function.
static const SigInfo* export_sig(const ModuleInfo* mi, uint32_t func_index) {
    if ((int)func_index < mi->nimportedFuncs) return NULL;
    uint32_t def = func_index - (uint32_t)mi->nimportedFuncs;
    if ((int)def >= mi->nfuncs) return NULL;
    uint32_t t = mi->funcType[def];
    if ((int)t >= mi->ntypes) return NULL;
    return &mi->types[t];
}

// ---------------------------------------------------------------------------
// JS object plumbing
// ---------------------------------------------------------------------------
JSClassID g_cls_module = 0;    // WebAssembly.Module -> ModuleInfo + bytes
JSClassID g_cls_instance = 0;  // WebAssembly.Instance -> InstanceState
JSClassID g_cls_memory = 0;    // WebAssembly.Memory -> runtime handle

struct ModuleState {
    ModuleInfo info;
    uint8_t* bytes;
    size_t len;
};

// The glue imports. bloxd regenerates both the import-module name and every
// field name on each upstream build, so the set is discovered from the parsed
// module at link time; only the slot storage is fixed here.
static const int kMaxImportSlots = 24;
static const int kMaxImportArgs  = 6;

struct ImportSlot {
    JSContext* ctx;
    JSValue fn;  // owns a reference, freed on instance teardown
    int nargs;
    int nrets;
    bool returns_i32;
    bool live;
    char name[56];  // module!field, for NF_WASM_TRACE=2 call tracing
    int dumped;     // one-shot source dump guard
};

struct ImportSet {
    ImportSlot slots[kMaxImportSlots];
};

struct InstanceState {
    IM3Runtime rt;
    ModuleState* mod;  // borrowed, kept alive by the JS Module object
    ImportSet imports; // glue callbacks; owns one JS reference per live slot
    struct MemoryState* mem; // owned by the instance's WebAssembly.Memory object
};

struct MemoryState {
    InstanceState* inst;  // borrowed
    // The last ArrayBuffer handed out for memory.buffer. Real WebAssembly
    // detaches an older view when linear memory grows; wasm-bindgen's glue
    // relies on that (it caches a typed array and only rebuilds when the
    // cached one reports byteLength 0). A zero-copy QuickJS buffer over the
    // interpreter's memory never detaches by itself, so the glue kept using a
    // stale view over freed memory and every exported [ptr,len] table read
    // garbage. Detaching it here restores the real semantics.
    JSValue last_buf;
    // Geometry of the buffer currently handed out. A real WebAssembly.Memory
    // returns the same ArrayBuffer object until linear memory grows, and only
    // then detaches the old one. Detaching on every `.buffer` read broke the
    // glue, which keeps typed views alive: "ArrayBuffer is detached".
    uint32_t last_size;
    uint8_t* last_base;
};

static void module_free(void* p) {
    ModuleState* m = (ModuleState*)p;
    if (!m) return;
    free(m->bytes);
    free(m);
}

// Defined next to the memory object further down; the export thunk needs it
// right after every m3_Call, so it is declared here.
static void memory_drop_stale_buffer(JSContext* ctx, InstanceState* inst);

// wasm3 lays out a raw import call as sp[0..nrets-1] holding the result slot(s)
// followed by the arguments (m3_exec.h: `u64 * args = sp + nRets`). Reading
// arguments from sp[0] therefore fed the garbage result slot into JS and dropped
// the last real argument, and every import with a return value wrote its result
// over the wrong cell. Draco's memcpy import (a!b, three args -> i32) copied
// nothing that way, and its decoder then looped forever.
static JSValue js_call_slot(ImportSlot* s, int nrets, uint64_t* sp) {
    uint64_t* a = sp + nrets;
    int n = s->nargs;
    if (n < 0) n = 0;
    if (n > kMaxImportArgs) n = kMaxImportArgs;
    if (wasm_trace2()) {
        unsigned long long a0 = n > 0 ? (unsigned long long)(unsigned)(uint32_t)a[0] : 0ull;
        unsigned long long a1 = n > 1 ? (unsigned long long)(unsigned)(uint32_t)a[1] : 0ull;
        unsigned long long a2 = n > 2 ? (unsigned long long)(unsigned)(uint32_t)a[2] : 0ull;
        WLOG("wasm: callin %s argc=%d a0=%llu a1=%llu a2=%llu\n", s->name, n, a0, a1, a2);
    }
    if (s->dumped == 0 && JS_IsFunction(s->ctx, s->fn)) {
        // One-shot source dump: the minified glue keeps no readable names, so
        // the body text is the only way to tell which callback a hang sits in.
        s->dumped = 1;
        JSValue src = JS_ToString(s->ctx, s->fn);
        const char* txt = JS_ToCString(s->ctx, src);
        if (txt) {
            char flat[620];
            size_t k = 0;
            for (size_t i = 0; txt[i] && k + 2 < sizeof(flat); i++) {
                char ch = txt[i];
                flat[k++] = (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch;
            }
            flat[k] = 0;
            WLOG("wasm: src %s => %s\n", s->name, flat);
            JS_FreeCString(s->ctx, txt);
        }
        JS_FreeValue(s->ctx, src);
    }
    JSValue args[kMaxImportArgs];
    for (int i = 0; i < n; i++)
        args[i] = JS_NewInt32(s->ctx, (int32_t)(uint32_t)a[i]);
    JSValue r = JS_Call(s->ctx, s->fn, JS_UNDEFINED, n, args);
    for (int i = 0; i < n; i++) JS_FreeValue(s->ctx, args[i]);
    // A JS exception cannot unwind through wasm3's C frames, so it must be
    // consumed here. Leaving it pending was worse than swallowing it: QuickJS
    // hands the stale exception to the next unrelated JS call, which is how a
    // TypeError raised inside an import surfaced later as a bogus
    // "operand 'prototype' property is not an object" from the glue.
    if (JS_HasException(s->ctx)) {
        JSValue exc = JS_GetException(s->ctx);
        if (wasm_trace()) {
            const char* msg = JS_ToCString(s->ctx, exc);
            WLOG("wasm: import threw: %s\n", msg ? msg : "?");
            if (msg) JS_FreeCString(s->ctx, msg);
        }
        JS_FreeValue(s->ctx, exc);
        JS_FreeValue(s->ctx, r);
        return JS_UNDEFINED;
    }
    return r;
}

// mjwb(i32) - wasm-bindgen object_drop_ref: call the table entry and release it.
static const void* cb_mjwb(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return NULL;
    JSValue r = js_call_slot(s, s->nrets, sp);
    JS_FreeValue(s->ctx, r);
    if (s->nrets > 0) sp[0] = 0;
    return NULL;
}

// A shared import returning i32: the JS value lands in sp[0] as wasm3 expects.
static const void* cb_wi08(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return NULL;
    JSValue r = js_call_slot(s, 1, sp);
    int32_t out = 0;
    if (!JS_IsException(r) && !JS_IsUndefined(r)) {
        int32_t tmp = 0;
        JS_ToInt32(s->ctx, &tmp, r);
        out = tmp;
    } else {
        JS_FreeValue(s->ctx, JS_GetException(s->ctx));
    }
    JS_FreeValue(s->ctx, r);
    sp[0] = (uint64_t)(uint32_t)out;
    return NULL;
}

// hbca(i32) - throw a handle's exception.
static const void* cb_hbca(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return NULL;
    JSValue r = js_call_slot(s, 0, sp);
    JS_FreeValue(s->ctx, r);
    return NULL;
}

// Console.error forwarder.
static const void* cb_uDZB(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return NULL;
    JSValue r = js_call_slot(s, 0, sp);
    JS_FreeValue(s->ctx, r);
    return NULL;
}

// KmDO() -> f64 - monotonic clock for the glue.
static const void* cb_KmDO(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return NULL;
    JSValue r = JS_Call(s->ctx, s->fn, JS_UNDEFINED, 0, NULL);
    double d = 0;
    if (!JS_IsException(r)) JS_ToFloat64(s->ctx, &d, r);
    else JS_FreeValue(s->ctx, JS_GetException(s->ctx));
    JS_FreeValue(s->ctx, r);
    // wasm3 reads the slot as f64, so store the bit pattern.
    union { double d; uint64_t u; } cv;
    cv.d = d;
    sp[0] = cv.u;
    return NULL;
}

// mjRe(i32,i32) - throw an Error; surfaces as a wasm trap.
static const void* cb_mjRe(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)mem;
    ImportSlot* s = (ImportSlot*)ictx->userdata;
    if (!s || !s->live) return m3Err_trapAbort;
    JSValue r = js_call_slot(s, 0, sp);
    JS_FreeValue(s->ctx, r);
    return m3Err_trapAbort;
}

// ---------------------------------------------------------------------------
// exported function thunk
// ---------------------------------------------------------------------------
// Every export is wrapped with the same native function; the per-function data
// is the owning InstanceState plus the resolved signature, so the JS side never
// has to carry wasm typing around.
//
// QuickJS only lets JS_GetOpaque match its own class id, and a C function
// object carries a private class, so the thunk payload is reached through the
// `magic` word instead: it indexes a small side table that lives for the
// process lifetime (the browser tears everything down at navigation anyway).
struct ThunkData {
    JSContext* ctx;
    InstanceState* inst;
    SigInfo sig;
    char name[48];
    bool ok;
};

const int kMaxThunks = 65536;
static ThunkData* g_thunks[kMaxThunks];
static int g_nthunks = 0;

static ThunkData* thunk_of(int magic) {
    if (magic < 0 || magic >= g_nthunks) return NULL;
    return g_thunks[magic];
}

// Marshal JS arguments into wasm and hand results back. Values travel through
// a small union array because wasm3 wants a per-argument pointer.
static JSValue thunk_call(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv, int magic) {
    (void)this_val;
    ThunkData* td = thunk_of(magic);
    if (!td || !td->ok) return JS_EXCEPTION;

    // bloxd's mesher entry takes well over 16 parameters; clamping the count
    // made m3_Call reject the call with "argument count mismatch", which is what
    // killed every meshing worker right after it started using the wasm mesher.
    union Slot {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } slots[64];
    const void* argp[64];

    IM3Function fn = NULL;
    M3Result r = m3_FindFunction(&fn, td->inst->rt, td->name);
    if (r) {
        // A lookup failure is either "the name really is not exported" or
        // "the runtime under this thunk belongs to a different module". Our
        // own export table knows which it is, and the difference decides
        // whether to fix the module bytes or the instance lifetime.
        const ModuleInfo* dmi = (td->inst && td->inst->mod) ? &td->inst->mod->info : NULL;
        int has = -1;
        if (dmi) {
            has = 0;
            for (int q = 0; q < dmi->nexports; q++) {
                if (strcmp(dmi->expName[q], td->name) == 0) { has = 1; break; }
            }
        }
        fprintf(stderr, "\n[WEXP] name=%s rt=%p nexports=%d has=%d\n", td->name,
                (void*)(td->inst ? td->inst->rt : NULL), dmi ? dmi->nexports : -1, has);
        fflush(stderr);
        WLOG("wasm: find %s -> %s\n", td->name, r);
        return JS_ThrowInternalError(ctx, "wasm export %s: %s", td->name, r);
    }
    // wasm3 holds the authoritative function type. Marshalling from our own
    // byte-level signature parse broke on reference/multi-value signatures,
    // surfacing as "argument count mismatch" from m3_GetResults on real
    // modules, so argument and result counts come from the runtime here.
    uint32_t nwasm = m3_GetArgCount(fn);
    if (nwasm > 64) {
        // Beyond the marshalling array: refuse rather than silently send a
        // wrong argument count and get an opaque runtime error.
        WLOG("wasm: %s has %u args, unsupported\n", td->name, nwasm);
        return JS_ThrowInternalError(ctx, "wasm export %s: too many arguments", td->name);
    }
    if (wasm_trace()) {
        uint32_t msz = 0;
        m3_GetMemory(td->inst->rt, &msz, 0);
        WLOG("wasm: fn %s argc=%u retc=%u ret0=%d mem=%u\n", td->name, nwasm,
             m3_GetRetCount(fn), (int)m3_GetRetType(fn, 0), msz);
    }
    for (uint32_t i = 0; i < nwasm; i++) {
        M3ValueType vt = m3_GetArgType(fn, (uint32_t)i);
        if ((int)i < argc) {
            switch (vt) {
                case c_m3Type_i32: {
                    int32_t v = 0;
                    JS_ToInt32(ctx, &v, argv[i]);
                    slots[i].i32 = v;
                    break;
                }
                case c_m3Type_i64: {
                    int64_t v = 0;
                    JS_ToInt64Ext(ctx, &v, argv[i]);
                    slots[i].i64 = v;
                    break;
                }
                case c_m3Type_f32: {
                    double v = 0;
                    JS_ToFloat64(ctx, &v, argv[i]);
                    slots[i].f32 = (float)v;
                    break;
                }
                case c_m3Type_f64: {
                    double v = 0;
                    JS_ToFloat64(ctx, &v, argv[i]);
                    slots[i].f64 = v;
                    break;
                }
                default:
                    slots[i].i64 = 0;
                    break;
            }
        } else {
            slots[i].i64 = 0;
        }
        argp[i] = &slots[i];
    }
    // wasm3 runs interpreted, and a voxel frame can spend seconds inside a
    // single export without ever returning to QuickJS (where the deadline
    // interrupt could fire). Report the slow ones by name so frame time can be
    // attributed to wasm instead of guessed at.
    clock_t c0 = clock();
    r = m3_Call(fn, nwasm, argp);
    {
        double ms = (double)(clock() - c0) * 1000.0 / (double)CLOCKS_PER_SEC;
        static int slow_n = 0;
        // Unconditional: a wasm export that takes seconds is the answer to
        // "why did this frame never return", and gating it behind a trace
        // variable turned that evidence into a silent zero.
        if (ms > 200.0 && slow_n < 160) {
            slow_n++;
            fprintf(stderr, "\njs: wasm3: SLOW %.0fms %s\n", ms, td->name);
            fflush(stderr);
        }
    }
    if (r) {
        WLOG("wasm: call %s -> %s\n", td->name, r);
        return JS_ThrowInternalError(ctx, "wasm call %s: %s", td->name, r);
    }

    memory_drop_stale_buffer(ctx, td->inst);
    uint32_t nret = m3_GetRetCount(fn);
    if (nret == 0) {
        if (wasm_trace()) WLOG("wasm: ok %s(%u) -> void\n", td->name, nwasm);
        return JS_UNDEFINED;
    }
    if (nret > 4) return JS_UNDEFINED;

    union RSlot {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } rets[4];
    const void* retp[4];
    for (uint32_t i = 0; i < nret; i++) {
        rets[i].i64 = 0;
        retp[i] = &rets[i];
    }
    r = m3_GetResults(fn, nret, retp);
    if (r) {
        WLOG("wasm: result %s -> %s\n", td->name, r);
        return JS_ThrowInternalError(ctx, "wasm result %s: %s", td->name, r);
    }

    // Rust/wasm-bindgen exports frequently return a multi-value tuple such as
    // (ptr, len). QuickJS has no multi-value return, so expose the tuple as a
    // plain JS array; bloxd's glue reads it as `oo[0]`, `oo[1]`, `oo[2]`.
    if (nret > 1) {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) return arr;
        for (uint32_t i = 0; i < nret; i++) {
            JSValue e;
            switch (m3_GetRetType(fn, i)) {
                case c_m3Type_i32: e = JS_NewInt32(ctx, rets[i].i32); break;
                case c_m3Type_i64: e = JS_NewInt64(ctx, rets[i].i64); break;
                case c_m3Type_f32: e = JS_NewFloat64(ctx, (double)rets[i].f32); break;
                case c_m3Type_f64: e = JS_NewFloat64(ctx, rets[i].f64); break;
                default:           e = JS_UNDEFINED; break;
            }
            JS_SetPropertyUint32(ctx, arr, i, e);
        }
        if (wasm_trace()) {
            WLOG("wasm: ok %s(%u) -> %u values [", td->name, nwasm, nret);
            for (uint32_t i = 0; i < nret; i++) {
                WLOG("%s%d", i ? "," : "", rets[i].i32);
            }
            WLOG("]\n");
        }
        return arr;
    }

    switch (m3_GetRetType(fn, 0)) {
        case c_m3Type_i32:
            if (wasm_trace()) WLOG("wasm: ok %s(%u) -> i32 %d\n", td->name, nwasm, rets[0].i32);
            return JS_NewInt32(ctx, rets[0].i32);
        case c_m3Type_i64:
            if (wasm_trace()) WLOG("wasm: ok %s(%u) -> i64\n", td->name, nwasm);
            return JS_NewInt64(ctx, rets[0].i64);
        case c_m3Type_f32:
            if (wasm_trace()) WLOG("wasm: ok %s(%u) -> f32\n", td->name, nwasm);
            return JS_NewFloat64(ctx, (double)rets[0].f32);
        case c_m3Type_f64:
            if (wasm_trace()) WLOG("wasm: ok %s(%u) -> f64\n", td->name, nwasm);
            return JS_NewFloat64(ctx, rets[0].f64);
        default:
            if (wasm_trace()) WLOG("wasm: ok %s(%u) -> void\n", td->name, nwasm);
            return JS_UNDEFINED;
    }
}

static void thunk_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt; (void)val;
    // ThunkData is arena-owned by its InstanceState; nothing to release here.
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
static JSValue memory_get_buffer(JSContext* ctx, JSValueConst this_val, int argc,
                                  JSValueConst* argv, int magic) {
    (void)argc; (void)argv; (void)magic;
    MemoryState* ms = (MemoryState*)JS_GetOpaque(this_val, g_cls_memory);
    if (!ms || !ms->inst || !ms->inst->rt) return JS_NULL;
    uint32_t size = 0;
    uint8_t* base = m3_GetMemory(ms->inst->rt, &size, 0);
    if (wasm_trace()) WLOG("wasm: memory buffer %p size=%u\n", (void*)base, size);
    if (!base || !size) return JS_NULL;
    // Real semantics: `.buffer` keeps returning the same ArrayBuffer object
    // until linear memory grows. Only a growth (which may also relocate the
    // backing store) detaches the old one -- that is the single moment the glue
    // is expected to rebuild its cached typed array. Detaching unconditionally
    // broke bloxd's engine with "ArrayBuffer is detached".
    if (!JS_IsUndefined(ms->last_buf) && ms->last_size == size && ms->last_base == base) {
        return JS_DupValue(ctx, ms->last_buf);
    }
    if (!JS_IsUndefined(ms->last_buf)) {
        JS_DetachArrayBuffer(ctx, ms->last_buf);
        JS_FreeValue(ctx, ms->last_buf);
        ms->last_buf = JS_UNDEFINED;
    }
    JSValue buf = JS_NewArrayBuffer(ctx, base, size, NULL, NULL, false);
    if (JS_IsException(buf)) return buf;
    ms->last_buf = JS_DupValue(ctx, buf);
    ms->last_size = size;
    ms->last_base = base;
    return buf;
}

// WebAssembly.Memory.prototype.grow(delta) -> previous page count, -1 on failure.
// Draco's elided-allocator grows linear memory from JS through this call; with
// no such method its wrapper throws, the allocation never succeeds, and the
// decoder retries forever inside wasm (a full core, no progress).
static JSValue memory_grow(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv, int magic) {
    (void)magic;
    MemoryState* ms = (MemoryState*)JS_GetOpaque(this_val, g_cls_memory);
    if (!ms || !ms->inst || !ms->inst->rt) return JS_NewInt32(ctx, -1);
    int32_t delta = 0;
    if (argc > 0) {
        if (JS_ToInt32(ctx, &delta, argv[0])) return JS_EXCEPTION;
    }
    if (delta < 0) {
        JS_ThrowRangeError(ctx, "WebAssembly.Memory.grow: negative delta");
        return JS_EXCEPTION;
    }
    uint32_t size0 = 0;
    m3_GetMemory(ms->inst->rt, &size0, 0);
    uint32_t pages0 = size0 / 65536;
    M3Result r = ResizeMemory(ms->inst->rt, pages0 + (uint32_t)delta);
    if (r && wasm_trace()) WLOG("wasm: grow(%u) -> %s\n", (unsigned)delta, r);
    if (r) return JS_NewInt32(ctx, -1);
    // Growing may relocate the backing store, so the cached ArrayBuffer is
    // stale; detaching it is what makes the glue notice and rebuild HEAP views.
    if (!JS_IsUndefined(ms->last_buf)) {
        JS_DetachArrayBuffer(ctx, ms->last_buf);
        JS_FreeValue(ctx, ms->last_buf);
        ms->last_buf = JS_UNDEFINED;
    }
    ms->last_base = NULL;
    ms->last_size = 0;
    return JS_NewInt32(ctx, (int32_t)pages0);
}

static JSValue memory_get_byte_length(JSContext* ctx, JSValueConst this_val, int argc,
                                      JSValueConst* argv, int magic) {
    (void)argc; (void)argv; (void)magic;
    MemoryState* ms = (MemoryState*)JS_GetOpaque(this_val, g_cls_memory);
    if (!ms || !ms->inst || !ms->inst->rt) return JS_NewInt32(ctx, 0);
    uint32_t size = 0;
    m3_GetMemory(ms->inst->rt, &size, 0);
    return JS_NewInt32(ctx, (int32_t)size);
}

// ---------------------------------------------------------------------------
// environment + import linking
// ---------------------------------------------------------------------------
// One environment is shared by every instance in this JS context; wasm3 keeps
// per-module state in the runtime, so this is the cheap half of the setup.
static IM3Environment g_env = NULL;

static char m3_sig_char(uint8_t t) {
    switch (t) {
        case c_m3Type_i32: return 'i';
        case c_m3Type_i64: return 'I';
        case c_m3Type_f32: return 'f';
        case c_m3Type_f64: return 'F';
        default:           return 'v';
    }
}

// Build wasm3's signature spelling ("v(i)", "i(ii)", "F()") from the parsed
// import's own function type. Spelling it by hand is what broke: upstream
// reloads rename the imports, and a stale hardcoded string silently links
// nothing.
static void build_sig(IM3Function f, char* out, size_t cap) {
    size_t n = 0;
    uint32_t nret = m3_GetRetCount(f);
    if (nret == 0) {
        if (n + 1 < cap) out[n++] = 'v';
    } else {
        for (uint32_t i = 0; i < nret && n + 1 < cap; i++) {
            out[n++] = m3_sig_char(m3_GetRetType(f, i));
        }
    }
    if (n + 1 < cap) out[n++] = '(';
    uint32_t nargs = m3_GetArgCount(f);
    for (uint32_t i = 0; i < nargs && n + 1 < cap; i++) {
        out[n++] = m3_sig_char(m3_GetArgType(f, i));
    }
    if (n + 1 < cap) out[n++] = ')';
    out[n] = 0;
}

// Stub for an import whose JS side is absent: write a zero result (i32/f32/f64
// all read as zero through this bit pattern) so the wasm keeps running instead
// of reading uninitialised stack space.
static const void* cb_zero(IM3Runtime rt, IM3ImportContext ictx, uint64_t* sp, void* mem) {
    (void)rt; (void)ictx; (void)mem;
    if (sp) sp[0] = 0;
    return NULL;
}

static bool link_glue(IM3Module mod, ImportSet* set, JSContext* ctx, JSValueConst imports) {
    // Names of both the import module and its fields are regenerated on every
    // upstream build (`ogZX!mjwb...` became `RcFR!igsX...`), so they are read
    // back out of the parsed module instead of assumed. The JS function behind
    // each name is looked up on the glue object handed to instantiate(), first
    // under the import-module name, then flat.
    int n = 0;
    for (uint32_t fi = 0; fi < mod->numFuncImports && n < kMaxImportSlots; fi++) {
        IM3Function f = &mod->functions[fi];
        if (!f || !f->import.fieldUtf8) continue;
        const char* mname = f->import.moduleUtf8 ? f->import.moduleUtf8 : "";
        const char* fname = f->import.fieldUtf8;

        char sig[48];
        build_sig(f, sig, sizeof(sig));
        uint32_t nargs = m3_GetArgCount(f);
        uint32_t nret = m3_GetRetCount(f);
        uint8_t rtype = nret ? m3_GetRetType(f, 0) : 0;

        M3RawCall cb;
        bool ret_i32 = false;
        if (nret == 1 && rtype == c_m3Type_i32)      { cb = cb_wi08; ret_i32 = true; }
        else if (nret == 1 && rtype == c_m3Type_f64) { cb = cb_KmDO; }
        else                                         { cb = cb_mjwb; }

        ImportSlot* slot = &set->slots[n];
        slot->ctx = ctx;
        slot->nargs = (int)(nargs > kMaxImportArgs ? kMaxImportArgs : nargs);
        slot->nrets = (int)(nret > 2 ? 2 : nret);
        slot->returns_i32 = ret_i32;
        slot->live = false;
        slot->fn = JS_UNDEFINED;
        slot->dumped = 0;
        snprintf(slot->name, sizeof(slot->name), "%s!%s", mname, fname);

        JSValue fn = JS_UNDEFINED;
        if (*mname) {
            JSValue ns = JS_GetPropertyStr(ctx, imports, mname);
            if (JS_IsObject(ns)) fn = JS_GetPropertyStr(ctx, ns, fname);
            JS_FreeValue(ctx, ns);
        }
        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            JSValue flat = JS_GetPropertyStr(ctx, imports, fname);
            if (JS_IsFunction(ctx, flat)) {
                fn = flat;
            } else {
                JS_FreeValue(ctx, flat);
                fn = JS_UNDEFINED;
            }
        }

        M3Result r;
        if (!JS_IsFunction(ctx, fn)) {
            // Not fatal: the wasm keeps running and reads back zeros.
            r = m3_LinkRawFunctionEx(mod, mname, fname, sig, cb_zero, NULL);
            if (r) WLOG("wasm: link-stub %s!%s %s -> %s\n", mname, fname, sig, r);
        } else {
            slot->fn = fn;  // keeps the reference
            slot->live = true;
            r = m3_LinkRawFunctionEx(mod, mname, fname, sig, cb, slot);
            if (r) {
                WLOG("wasm: link %s!%s %s -> %s\n", mname, fname, sig, r);
                JS_FreeValue(ctx, fn);
                slot->live = false;
                slot->fn = JS_UNDEFINED;
            } else if (wasm_trace()) {
                WLOG("wasm: linked %s!%s %s\n", mname, fname, sig);
            }
        }
        n++;
    }
    WLOG("wasm: glue imports tried=%d of %u\n", n, (unsigned)mod->numFuncImports);
    return true;
}


// ---------------------------------------------------------------------------
// exports
// ---------------------------------------------------------------------------
static JSValue make_memory_object(JSContext* ctx, InstanceState* inst) {
    JSValue mem = JS_NewObjectClass(ctx, g_cls_memory);
    if (JS_IsException(mem)) return mem;
    MemoryState* ms = (MemoryState*)calloc(1, sizeof(MemoryState));
    if (!ms) {
        JS_FreeValue(ctx, mem);
        return JS_EXCEPTION;
    }
    ms->inst = inst;
    ms->last_buf = JS_UNDEFINED;
    inst->mem = ms;
    JS_SetOpaque(mem, ms);
    JSAtom a1 = JS_NewAtom(ctx, "buffer");
    JSAtom a2 = JS_NewAtom(ctx, "byteLength");
    JS_DefinePropertyGetSet(ctx, mem, a1,
                            JS_NewCFunctionMagic(ctx, memory_get_buffer, "get buffer", 0,
                                                 JS_CFUNC_generic_magic, 0),
                            JS_UNDEFINED, JS_PROP_ENUMERABLE);
    JS_SetPropertyStr(ctx, mem, "grow",
                      JS_NewCFunctionMagic(ctx, memory_grow, "grow", 1,
                                           JS_CFUNC_generic_magic, 0));
    JS_DefinePropertyGetSet(ctx, mem, a2,
                            JS_NewCFunctionMagic(ctx, memory_get_byte_length, "get byteLength", 0,
                                                 JS_CFUNC_generic_magic, 0),
                            JS_UNDEFINED, JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, a1);
    JS_FreeAtom(ctx, a2);
    return mem;
}

// wasm-bindgen caches typed-array views over `memory.buffer` and only rebuilds
// them once the cached view reports byteLength 0, which a real engine arranges
// by detaching the old ArrayBuffer the moment linear memory grows. Growth
// performed by the module itself (the dlmalloc sbrk path) happens inside
// m3_Call, where our zero-copy buffer cannot notice on its own; the glue then
// kept writing into the pre-growth view - a 320 KiB Uint32Array - while the
// allocator handed out offsets past its end, which surfaced as
// "RangeError: invalid array length" from typedArray.set inside the mesher.
// Detaching here restores the engine's semantics right after every export call.
static void memory_drop_stale_buffer(JSContext* ctx, InstanceState* inst) {
    if (!inst || !inst->mem) return;
    MemoryState* ms = inst->mem;
    if (JS_IsUndefined(ms->last_buf)) return;
    uint32_t size = 0;
    uint8_t* base = m3_GetMemory(inst->rt, &size, 0);
    if (size == ms->last_size && base == ms->last_base) return;
    JS_DetachArrayBuffer(ctx, ms->last_buf);
    JS_FreeValue(ctx, ms->last_buf);
    ms->last_buf = JS_UNDEFINED;
    ms->last_size = 0;
    ms->last_base = NULL;
}

static JSValue make_thunk(JSContext* ctx, InstanceState* inst, const char* name,
                          const SigInfo* sig) {
    if (g_nthunks >= kMaxThunks) return JS_EXCEPTION;
    ThunkData* td = (ThunkData*)calloc(1, sizeof(ThunkData));
    if (!td) return JS_EXCEPTION;
    td->ctx = ctx;
    td->inst = inst;
    td->ok = sig != NULL;
    if (sig) td->sig = *sig;
    strncpy(td->name, name, sizeof(td->name) - 1);
    int idx = g_nthunks;
    g_thunks[g_nthunks++] = td;
    // The native function is the export itself; magic carries its table slot.
    JSValue fn = JS_NewCFunctionMagic(ctx, thunk_call, name, (int)td->sig.nparams,
                                      JS_CFUNC_generic_magic, idx);
    // QuickJS only materialises a `prototype` property for constructor-kind
    // C functions, so a generic C function reports "operand 'prototype'
    // property is not an object" the moment anything applies `instanceof` to
    // it. The wasm-bindgen glue does exactly that on the exports object, which
    // killed the boot right after instantiation. Give each thunk an inert
    // prototype object so those checks stay false instead of throwing.
    JS_DefinePropertyValueStr(ctx, fn, "prototype", JS_NewObject(ctx),
                              JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    return fn;
}

static JSValue build_exports(JSContext* ctx, InstanceState* inst) {
    JSValue ex = JS_NewObject(ctx);
    const ModuleInfo* mi = &inst->mod->info;
    for (int i = 0; i < mi->nexports; i++) {
        const char* nm = mi->expName[i];
        uint8_t kind = mi->expKind[i];
        if (kind == 0) {  // function
            const SigInfo* sig = export_sig(mi, mi->expIndex[i]);
            JSValue fn = make_thunk(ctx, inst, nm, sig);
            if (JS_IsException(fn)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                continue;
            }
            JS_SetPropertyStr(ctx, ex, nm, fn);
        } else if (kind == 2) {  // memory
            JSValue mem = make_memory_object(ctx, inst);
            if (JS_IsException(mem)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                continue;
            }
            JS_SetPropertyStr(ctx, ex, nm, mem);
        }
    }
    return ex;
}

// ---------------------------------------------------------------------------
// instance construction
// ---------------------------------------------------------------------------
struct InstanceBox {
    IM3Runtime rt;
    ImportSet imports;
};

static void free_instance_box(InstanceBox* ib) {
    if (!ib) return;
    for (int i = 0; i < kMaxImportSlots; i++) {
        if (ib->imports.slots[i].live) {
            JS_FreeValue(ib->imports.slots[i].ctx, ib->imports.slots[i].fn);
            ib->imports.slots[i].live = false;
        }
    }
    if (ib->rt) {
        m3_FreeRuntime(ib->rt);
        ib->rt = NULL;
    }
    free(ib);
}

// Create an M3Runtime for a parsed module, link the six glue imports, and
// return the export bag. Returns JS_EXCEPTION on any hard failure.
static JSValue instantiate_from_state(JSContext* ctx, ModuleState* ms, JSValueConst imports) {
    // Instantiation walks every function body before the first call, so the
    // caller's setup code and its .then() chain must not be cut short.
    nfu_js_extend_deadline(60000u);
    if (!g_env) {
        g_env = m3_NewEnvironment();
        if (!g_env) return JS_ThrowInternalError(ctx, "wasm: no environment");
    }

    // Draco's decoder recurses deeply (its JS wrapper drives nested wasm calls);
    // the old 64 KB interpreter stack overflowed into unmapped memory and killed
    // the process mid-decode, which looked like a random access violation.
    IM3Runtime rt = m3_NewRuntime(g_env, 4 * 1024 * 1024, NULL);
    if (!rt) return JS_ThrowInternalError(ctx, "wasm: runtime allocation failed");
    WLOG("wasm: runtime %p\n", (void*)rt);

    IM3Module mod = NULL;
    M3Result r = m3_ParseModule(g_env, &mod, ms->bytes, (uint32_t)ms->len);
    if (r) {
        WLOG("wasm: parse -> %s\n", r);
        m3_FreeRuntime(rt);
        return JS_ThrowInternalError(ctx, "wasm: %s", r);
    }
    WLOG("wasm: parsed %p\n", (void*)mod);
    r = m3_LoadModule(rt, mod);
    if (r) {
        WLOG("wasm: load -> %s\n", r);
        m3_FreeModule(mod);
        m3_FreeRuntime(rt);
        return JS_ThrowInternalError(ctx, "wasm: %s", r);
    }
    WLOG("wasm: loaded\n");

    // The runtime is attached to the instance state before linking so the raw
    // callbacks can resolve their slots through it.
    InstanceState* inst = (InstanceState*)calloc(1, sizeof(InstanceState));
    if (!inst) {
        m3_FreeRuntime(rt);
        return JS_EXCEPTION;
    }
    inst->rt = rt;
    inst->mod = ms;

    link_glue(mod, &inst->imports, ctx, imports);
    WLOG("wasm: linked\n");

    JSValue ex = build_exports(ctx, inst);
    if (JS_IsException(ex)) {
        m3_FreeRuntime(rt);
        free(inst);
        return ex;
    }
    WLOG("wasm: exports built n=%d\n", ms->info.nexports);
    // The glue calls ft.<start>() immediately after instance creation. Report
    // whether that hook actually exists so a missing export is visible as a
    // missing name rather than as an opaque trap downstream.
    {
        JSValue k1 = JS_GetPropertyStr(ctx, ex, "KnuZ");
        JSValue k2 = JS_GetPropertyStr(ctx, ex, "__wbindgen_start");
        JSValue k3 = JS_GetPropertyStr(ctx, ex, "memory");
        WLOG("wasm: hooks KnuZ=%d start=%d memory=%d\n",
             JS_IsFunction(ctx, k1) ? 1 : 0,
             JS_IsFunction(ctx, k2) ? 1 : 0,
             JS_IsUndefined(k3) ? 0 : 1);
        JS_FreeValue(ctx, k1);
        JS_FreeValue(ctx, k2);
        JS_FreeValue(ctx, k3);
    }
    // Constructor prototype audit: QuickJS reports "operand 'prototype'
    // property is not an object" from `x instanceof C`, so verify that each
    // WebAssembly constructor really carries an object prototype.
    {
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue wa = JS_GetPropertyStr(ctx, g, "WebAssembly");
        JS_FreeValue(ctx, g);
        const char* names[4] = {"Module", "Instance", "Memory", "Table"};
        for (int i = 0; i < 4; i++) {
            JSValue c = JS_GetPropertyStr(ctx, wa, names[i]);
            JSValue pr = JS_GetPropertyStr(ctx, c, "prototype");
            WLOG("wasm: %s proto=%d\n", names[i],
                 JS_IsFunction(ctx, c) ? (JS_IsObject(pr) ? 2 : 1) : 0);
            JS_FreeValue(ctx, pr);
            JS_FreeValue(ctx, c);
        }
        JS_FreeValue(ctx, wa);
    }

    // The JS Instance object owns the runtime; a finalizer would be the right
    // place to release it, but the browser tears the whole context down at
    // navigation, so the state is intentionally retained for the page's life.
    // Created through the object class so it inherits Instance.prototype.
    JSValue obj = g_cls_instance ? JS_NewObjectClass(ctx, g_cls_instance) : JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "exports", ex);
    // Audit hook: if any step above left a pending exception behind, the next
    // JS statement in bloxd's glue inherits it and the failure shows up far
    // from its origin. Surface it here while the culpable step is still known.
    if (JS_HasException(ctx)) {
        JSValue pend = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, pend);
        WLOG("wasm: pending exception after instantiate: %s\n", msg ? msg : "?");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, pend);
    }
    return obj;
}

// ---------------------------------------------------------------------------
// byte extraction helpers
// ---------------------------------------------------------------------------
struct ByteView {
    const uint8_t* data;
    size_t len;
};

// Accept ArrayBuffer / TypedArray / DataView and report the underlying bytes.
static bool bytes_of(JSContext* ctx, JSValueConst v, ByteView* out) {
    size_t len = 0;
    uint8_t* p = JS_GetArrayBuffer(ctx, &len, v);
    if (p) {
        out->data = p;
        out->len = len;
        return true;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));  // clear the arraybuffer probe error
    JSValue buf = JS_GetPropertyStr(ctx, v, "buffer");
    if (!JS_IsUndefined(buf)) {
        JSValue boff = JS_GetPropertyStr(ctx, v, "byteOffset");
        JSValue blen = JS_GetPropertyStr(ctx, v, "byteLength");
        uint32_t off = 0, bl = 0;
        JS_ToUint32(ctx, &off, boff);
        JS_ToUint32(ctx, &bl, blen);
        JS_FreeValue(ctx, boff);
        JS_FreeValue(ctx, blen);
        size_t tl = 0;
        uint8_t* base = JS_GetArrayBuffer(ctx, &tl, buf);
        JS_FreeValue(ctx, buf);
        if (base && (size_t)off + bl <= tl) {
            out->data = base + off;
            out->len = bl;
            return true;
        }
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    return false;
}

// Pull bytes out of a Response: bloxd always fetches the wasm with `fetch()`,
// and the shim's Response keeps the payload synchronously available.
static bool bytes_of_response(JSContext* ctx, JSValueConst v, ByteView* out) {
    // The shim's Response materialises its payload synchronously, so the
    // ArrayBuffer already sits on the object. Digging it out directly avoids
    // depending on a promise that only settles on the next timer tick.
    static const char* kFast[] = {"_ab", "__nfuBytes"};
    for (int i = 0; i < 2; i++) {
        JSValue cached = JS_GetPropertyStr(ctx, v, kFast[i]);
        if (!JS_IsUndefined(cached) && !JS_IsNull(cached)) {
            bool ok = bytes_of(ctx, cached, out);
            JS_FreeValue(ctx, cached);
            if (ok && out->len > 0) return true;
        } else {
            JS_FreeValue(ctx, cached);
        }
    }
    JSValue ab = JS_GetPropertyStr(ctx, v, "arrayBuffer");
    if (JS_IsFunction(ctx, ab)) {
        JSValue pr = JS_Call(ctx, ab, v, 0, NULL);
        JS_FreeValue(ctx, ab);
        if (JS_IsException(pr)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return false;
        }
        JSValue res = pr;
        bool own_res = false;
        // The shim's Response is synchronous, so the promise is already
        // settled by the time we get here; anything else means the caller is
        // awaiting real I/O that this engine cannot block on.
        if (JS_PromiseState(ctx, pr) == JS_PROMISE_FULFILLED) {
            res = JS_PromiseResult(ctx, pr);
            own_res = true;
        } else {
            JS_FreeValue(ctx, pr);
            return false;
        }
        bool ok = bytes_of(ctx, res, out);
        if (own_res) JS_FreeValue(ctx, res);
        JS_FreeValue(ctx, pr);
        return ok;
    }
    JS_FreeValue(ctx, ab);
    return false;
}

// ---------------------------------------------------------------------------
// WebAssembly.Module
// ---------------------------------------------------------------------------
static JSValue js_module_ctor(JSContext* ctx, JSValueConst new_target, int argc,
                              JSValueConst* argv, int magic) {
    (void)new_target; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.Module needs bytes");
    ByteView bv;
    if (!bytes_of(ctx, argv[0], &bv))
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: argument is not a BufferSource");
    if (bv.len < 8)
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: truncated module");

    ModuleState* ms = (ModuleState*)calloc(1, sizeof(ModuleState));
    if (!ms) return JS_EXCEPTION;
    ms->len = bv.len;
    ms->bytes = (uint8_t*)malloc(bv.len);
    if (!ms->bytes) {
        free(ms);
        return JS_EXCEPTION;
    }
    memcpy(ms->bytes, bv.data, bv.len);
    if (!parse_module_info(ms->bytes, ms->len, &ms->info)) {
        free(ms->bytes);
        free(ms);
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: malformed module");
    }
    JSValue obj = JS_NewObjectClass(ctx, g_cls_module);
    if (JS_IsException(obj)) {
        module_free(ms);
        return obj;
    }
    JS_SetOpaque(obj, ms);
    return obj;
}

static JSValue js_module_exports(JSContext* ctx, JSValueConst this_val, int argc,
                                 JSValueConst* argv, int magic) {
    (void)argc; (void)argv; (void)magic;
    ModuleState* ms = (ModuleState*)JS_GetOpaque(this_val, g_cls_module);
    if (!ms) return JS_ThrowTypeError(ctx, "not a WebAssembly.Module");
    JSValue arr = JS_NewArray(ctx);
    int n = 0;
    for (int i = 0; i < ms->info.nexports; i++) {
        JSValue d = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, d, "name", JS_NewString(ctx, ms->info.expName[i]));
        const char* k = ms->info.expKind[i] == 0 ? "function"
                      : ms->info.expKind[i] == 2 ? "memory" : "other";
        JS_SetPropertyStr(ctx, d, "kind", JS_NewString(ctx, k));
        JS_SetPropertyUint32(ctx, arr, (uint32_t)n++, d);
    }
    return arr;
}

// ---------------------------------------------------------------------------
// WebAssembly.Instance
// ---------------------------------------------------------------------------
static JSValue js_instance_ctor(JSContext* ctx, JSValueConst new_target, int argc,
                                JSValueConst* argv, int magic) {
    (void)new_target; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.Instance needs a Module");
    ModuleState* ms = (ModuleState*)JS_GetOpaque(argv[0], g_cls_module);
    if (!ms) return JS_ThrowTypeError(ctx, "WebAssembly.Instance: first argument is not a Module");
    JSValue imports = argc > 1 ? argv[1] : JS_UNDEFINED;
    return instantiate_from_state(ctx, ms, imports);
}

// ---------------------------------------------------------------------------
// WebAssembly.instantiate  (always returns a Promise, like the real API)
// ---------------------------------------------------------------------------
static JSValue make_resolved_promise(JSContext* ctx, JSValue value) {
    JSValue funcs[2];
    JSValue pr = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(pr)) return pr;
    JSValue r = JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, &value);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    return pr;
}

static JSValue make_rejected_promise(JSContext* ctx, JSValue reason) {
    JSValue funcs[2];
    JSValue pr = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(pr)) return pr;
    JSValue r = JS_Call(ctx, funcs[1], JS_UNDEFINED, 1, &reason);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    return pr;
}

static JSValue js_instantiate(JSContext* ctx, JSValueConst this_val, int argc,
                              JSValueConst* argv, int magic) {
    (void)this_val; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.instantiate needs a module");
    JSValueConst src = argv[0];
    JSValue imports = argc > 1 ? argv[1] : JS_UNDEFINED;
    WLOG("wasm: instantiate argc=%d\n", argc);

    // The module argument may be a compiled Module or raw bytes.
    ModuleState* ms = (ModuleState*)JS_GetOpaque(src, g_cls_module);
    ModuleState* owned = NULL;
    if (!ms) {
        ByteView bv;
        if (!bytes_of(ctx, src, &bv))
            return make_rejected_promise(ctx,
                JS_ThrowTypeError(ctx, "WebAssembly.instantiate: not a BufferSource"));
        owned = (ModuleState*)calloc(1, sizeof(ModuleState));
        if (!owned) return JS_EXCEPTION;
        owned->len = bv.len;
        owned->bytes = (uint8_t*)malloc(bv.len);
        if (!owned->bytes) {
            free(owned);
            return JS_EXCEPTION;
        }
        memcpy(owned->bytes, bv.data, bv.len);
        if (!parse_module_info(owned->bytes, owned->len, &owned->info)) {
            free(owned->bytes);
            free(owned);
            return make_rejected_promise(ctx,
                JS_ThrowTypeError(ctx, "WebAssembly.instantiate: malformed module"));
        }
        ms = owned;
    }

    JSValue r = instantiate_from_state(ctx, ms, imports);
    if (JS_IsException(r)) {
        JSValue err = JS_GetException(ctx);
        if (owned) module_free(owned);
        return make_rejected_promise(ctx, err);
    }

    // `WebAssembly.instantiate(bytes, imports)` resolves to
    // { module, instance }; `instantiate(Module, imports)` to the instance.
    bool raw_bytes = (owned != NULL);
    JSValue result;
    if (raw_bytes) {
        result = JS_NewObject(ctx);
        JSValue mod_obj = JS_NewObjectClass(ctx, g_cls_module);
        if (JS_IsException(mod_obj)) {
            JS_FreeValue(ctx, r);
            return make_rejected_promise(ctx, JS_GetException(ctx));
        }
        JS_SetOpaque(mod_obj, owned);
        owned = NULL;  // ownership moved into the JS object
        JS_SetPropertyStr(ctx, result, "module", mod_obj);
        JS_SetPropertyStr(ctx, result, "instance", r);
    } else {
        result = r;
    }
    WLOG("wasm: instantiate done raw=%d\n", raw_bytes ? 1 : 0);
    return make_resolved_promise(ctx, result);
}

// `WebAssembly.compile(bytes)` hands back a Module, not {module, instance}.
// bloxd's loader compiles once and instantiates separately, so returning the
// instantiate shape here used to break the very next step.
static JSValue module_from_bytes(JSContext* ctx, const uint8_t* data, size_t len) {
    // Parsing, copying and then re-wrapping this module happens inside one JS
    // tick, and the page chains several more steps onto it as microtasks.
    nfu_js_extend_deadline(60000u);
    ModuleState* ms = (ModuleState*)calloc(1, sizeof(ModuleState));
    if (!ms) return JS_EXCEPTION;
    ms->len = len;
    ms->bytes = (uint8_t*)malloc(len ? len : 1);
    if (!ms->bytes) { free(ms); return JS_EXCEPTION; }
    memcpy(ms->bytes, data, len);
    if (!parse_module_info(ms->bytes, ms->len, &ms->info)) {
        free(ms->bytes);
        free(ms);
        return JS_ThrowTypeError(ctx, "WebAssembly.compile: malformed module");
    }
    JSValue mod_obj = JS_NewObjectClass(ctx, g_cls_module);
    if (JS_IsException(mod_obj)) {
        free(ms->bytes);
        free(ms);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(mod_obj, ms);
    return mod_obj;
}

static JSValue js_compile(JSContext* ctx, JSValueConst this_val, int argc,
                          JSValueConst* argv, int magic) {
    (void)this_val; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.compile needs a BufferSource");
    WLOG("wasm: compile argc=%d\n", argc);
    ModuleState* ms = (ModuleState*)JS_GetOpaque(argv[0], g_cls_module);
    if (ms) return make_resolved_promise(ctx, JS_DupValue(ctx, argv[0]));
    ByteView bv;
    if (!bytes_of(ctx, argv[0], &bv))
        return make_rejected_promise(ctx,
            JS_ThrowTypeError(ctx, "WebAssembly.compile: not a BufferSource"));
    WLOG("wasm: compile got %d bytes\n", (int)bv.len);
    JSValue mod_obj = module_from_bytes(ctx, bv.data, bv.len);
    if (JS_IsException(mod_obj))
        return make_rejected_promise(ctx, JS_GetException(ctx));
    return make_resolved_promise(ctx, mod_obj);
}

// Streaming variant: same result, but the bytes come out of the fetch
// Response the shim materialises synchronously.
static JSValue js_compile_streaming(JSContext* ctx, JSValueConst this_val, int argc,
                                    JSValueConst* argv, int magic) {
    (void)this_val; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.compileStreaming needs a Response");
    WLOG("wasm: compileStreaming argc=%d\n", argc);
    ByteView bv;
    if (!bytes_of_response(ctx, argv[0], &bv)) {
        WLOG("wasm: compileStreaming no bytes\n");
        return make_rejected_promise(ctx,
            JS_ThrowTypeError(ctx, "WebAssembly.compileStreaming: response has no bytes"));
    }
    WLOG("wasm: compileStreaming got %d bytes\n", (int)bv.len);
    JSValue mod_obj = module_from_bytes(ctx, bv.data, bv.len);
    if (JS_IsException(mod_obj))
        return make_rejected_promise(ctx, JS_GetException(ctx));
    return make_resolved_promise(ctx, mod_obj);
}

static JSValue js_instantiate_streaming(JSContext* ctx, JSValueConst this_val, int argc,
                                        JSValueConst* argv, int magic) {
    (void)this_val; (void)magic;
    if (argc < 1) return JS_ThrowTypeError(ctx, "WebAssembly.instantiateStreaming needs a Response");
    WLOG("wasm: streaming argc=%d\n", argc);
    ByteView bv;
    if (!bytes_of_response(ctx, argv[0], &bv)) {
        WLOG("wasm: streaming no bytes\n");
        return make_rejected_promise(ctx,
            JS_ThrowTypeError(ctx, "WebAssembly.instantiateStreaming: response has no bytes"));
    }
    WLOG("wasm: streaming got %d bytes\n", (int)bv.len);
    ModuleState* ms = (ModuleState*)calloc(1, sizeof(ModuleState));
    if (!ms) return JS_EXCEPTION;
    ms->len = bv.len;
    ms->bytes = (uint8_t*)malloc(bv.len);
    if (!ms->bytes) {
        free(ms);
        return JS_EXCEPTION;
    }
    memcpy(ms->bytes, bv.data, bv.len);
    if (!parse_module_info(ms->bytes, ms->len, &ms->info)) {
        free(ms->bytes);
        free(ms);
        return make_rejected_promise(ctx,
            JS_ThrowTypeError(ctx, "WebAssembly.instantiateStreaming: malformed module"));
    }

    JSValue imports = argc > 1 ? argv[1] : JS_UNDEFINED;
    JSValue r = instantiate_from_state(ctx, ms, imports);
    if (JS_IsException(r)) {
        JSValue err = JS_GetException(ctx);
        module_free(ms);
        return make_rejected_promise(ctx, err);
    }
    JSValue result = JS_NewObject(ctx);
    JSValue mod_obj = JS_NewObjectClass(ctx, g_cls_module);
    if (JS_IsException(mod_obj)) {
        JS_FreeValue(ctx, r);
        module_free(ms);
        return make_rejected_promise(ctx, JS_GetException(ctx));
    }
    JS_SetOpaque(mod_obj, ms);
    JS_SetPropertyStr(ctx, result, "module", mod_obj);
    JS_SetPropertyStr(ctx, result, "instance", r);
    return make_resolved_promise(ctx, result);
}

static JSValue js_validate(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv, int magic) {
    (void)this_val; (void)magic;
    if (argc < 1) return JS_FALSE;
    ByteView bv;
    if (!bytes_of(ctx, argv[0], &bv)) return JS_FALSE;
    ModuleInfo mi;
    bool ok = parse_module_info(bv.data, bv.len, &mi);
    return ok ? JS_TRUE : JS_FALSE;
}

// ---------------------------------------------------------------------------
// WebAssembly.Memory
// ---------------------------------------------------------------------------
static JSValue js_memory_ctor(JSContext* ctx, JSValueConst new_target, int argc,
                              JSValueConst* argv, int magic) {
    (void)new_target; (void)magic;
    // Standalone Memory objects are not used by bloxd (its module declares its
    // own memory), so a descriptor-like shell is enough to keep feature probes
    // such as `new WebAssembly.Memory({initial:1})` from throwing.
    JSValue obj = JS_NewObjectClass(ctx, g_cls_memory);
    if (JS_IsException(obj)) return obj;
    MemoryState* ms = (MemoryState*)calloc(1, sizeof(MemoryState));
    if (!ms) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    if (argc > 0) {
        JSValue init = JS_GetPropertyStr(ctx, argv[0], "initial");
        int32_t pages = 1;
        JS_ToInt32(ctx, &pages, init);
        JS_FreeValue(ctx, init);
        (void)pages;
    }
    ms->inst = NULL;
    ms->last_buf = JS_UNDEFINED;
    JS_SetOpaque(obj, ms);
    return obj;
}

static JSValue js_not_supported_ctor(JSContext* ctx, JSValueConst new_target, int argc,
                                     JSValueConst* argv, int magic) {
    (void)new_target; (void)magic; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "WebAssembly: feature not available in this engine");
}

// ---------------------------------------------------------------------------
// class registration + install
// ---------------------------------------------------------------------------
static void module_finalizer(JSRuntime* rt, JSValue v) {
    (void)rt;
    ModuleState* ms = (ModuleState*)JS_GetOpaque(v, g_cls_module);
    if (ms) module_free(ms);
}

static void register_classes(JSRuntime* rt) {
    JSClassDef def;
    if (!g_cls_module) {
        JS_NewClassID(&g_cls_module);
        memset(&def, 0, sizeof(def));
        def.class_name = "WebAssembly.Module";
        def.finalizer = module_finalizer;
        JS_NewClass(rt, g_cls_module, &def);
    }
    if (!g_cls_instance) {
        JS_NewClassID(&g_cls_instance);
        memset(&def, 0, sizeof(def));
        def.class_name = "WebAssembly.Instance";
        JS_NewClass(rt, g_cls_instance, &def);
    }
    if (!g_cls_memory) {
        JS_NewClassID(&g_cls_memory);
        memset(&def, 0, sizeof(def));
        def.class_name = "WebAssembly.Memory";
        JS_NewClass(rt, g_cls_memory, &def);
    }
}

static JSValue make_error_class(JSContext* ctx, JSValueConst global, const char* name,
                                const char* parent) {
    // Expose a plain Error subclass through JS_Eval so instanceof and message
    // construction behave exactly like a browser.
    char src[256];
    snprintf(src, sizeof(src),
             "(function(p){return class %s extends p { constructor(m){super(m); this.name='%s'; } };})",
             name, name);
    JSValue fn = JS_Eval(ctx, src, strlen(src), "<wasm-err>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(fn)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_UNDEFINED;
    }
    JSValue pv = JS_GetPropertyStr(ctx, global, parent);
    JSValue made = JS_Call(ctx, fn, JS_UNDEFINED, 1, &pv);
    JS_FreeValue(ctx, pv);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(made)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_UNDEFINED;
    }
    JS_SetPropertyStr(ctx, global, name, JS_DupValue(ctx, made));
    return made;
}

}  // namespace

// QuickJS only materialises a `prototype` property for ordinary JS closures,
// not for constructor-kind C functions. Without one, `x instanceof
// WebAssembly.Instance` fails with "operand 'prototype' property is not an
// object" -- which is exactly what killed the bloxd boot right after the glue
// received its instance. Every constructor we expose gets a plain object here.
static void give_prototype(JSContext* ctx, JSValue ctor) {
    JSValue pr = JS_NewObject(ctx);
    if (JS_SetPropertyStr(ctx, ctor, "prototype", pr) < 0) {
        // Keep the context exception-free even if the define failed.
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
}

// Returns a new reference to ctor.prototype, or undefined when there is none.
static JSValue prototype_of(JSContext* ctx, JSValueConst ctor) {
    return JS_GetPropertyStr(ctx, ctor, "prototype");
}

bool wasm_js_install(JSContext* ctx, JSValue global) {
    if (!ctx) return false;
    register_classes(JS_GetRuntime(ctx));

    JSValue wa = JS_NewObject(ctx);

    JSValue mod_ctor = JS_NewCFunctionMagic(ctx, js_module_ctor, "Module", 1,
                                            JS_CFUNC_constructor_magic, 0);
    give_prototype(ctx, mod_ctor);
    // Same reason as Instance below: a Module produced by js_module_ctor must
    // answer true to `x instanceof WebAssembly.Module`, otherwise the glue
    // "helpfully" re-wraps it with new WebAssembly.Module(moduleObject) and
    // fails with "argument is not a BufferSource".
    if (g_cls_module) {
        JSValue mp = prototype_of(ctx, mod_ctor);
        if (JS_IsObject(mp)) {
            JS_SetClassProto(ctx, g_cls_module, JS_DupValue(ctx, mp));
        }
        JS_FreeValue(ctx, mp);
    }
    JS_SetPropertyStr(ctx, wa, "Module", mod_ctor);

    JSValue inst_ctor = JS_NewCFunctionMagic(ctx, js_instance_ctor, "Instance", 1,
                                             JS_CFUNC_constructor_magic, 0);
    give_prototype(ctx, inst_ctor);
    // Bind the object class to the constructor's prototype so objects created
    // by instantiate_from_state satisfy `x instanceof WebAssembly.Instance`.
    // The wasm-bindgen glue relies on that test to decide whether the returned
    // value is `{instance, module}` or a bare instance, and a false negative
    // makes the destructuring yield undefined and kills the boot.
    if (g_cls_instance) {
        JSValue ip = prototype_of(ctx, inst_ctor);
        if (JS_IsObject(ip)) {
            JS_SetClassProto(ctx, g_cls_instance, JS_DupValue(ctx, ip));
        }
        JS_FreeValue(ctx, ip);
    }
    JS_SetPropertyStr(ctx, wa, "Instance", inst_ctor);

    JSValue mem_ctor = JS_NewCFunctionMagic(ctx, js_memory_ctor, "Memory", 1,
                                            JS_CFUNC_constructor_magic, 0);
    give_prototype(ctx, mem_ctor);
    if (g_cls_memory) {
        JSValue mp = prototype_of(ctx, mem_ctor);
        if (JS_IsObject(mp)) {
            JS_SetClassProto(ctx, g_cls_memory, JS_DupValue(ctx, mp));
        }
        JS_FreeValue(ctx, mp);
    }
    JS_SetPropertyStr(ctx, wa, "Memory", mem_ctor);

    JSValue table_ctor = JS_NewCFunctionMagic(ctx, js_not_supported_ctor, "Table", 1,
                                              JS_CFUNC_constructor_magic, 0);
    give_prototype(ctx, table_ctor);
    JS_SetPropertyStr(ctx, wa, "Table", table_ctor);

    JSValue global_ctor = JS_NewCFunctionMagic(ctx, js_not_supported_ctor, "Global", 1,
                                               JS_CFUNC_constructor_magic, 0);
    give_prototype(ctx, global_ctor);
    JS_SetPropertyStr(ctx, wa, "Global", global_ctor);

    JS_SetPropertyStr(ctx, wa, "instantiate",
                      JS_NewCFunctionMagic(ctx, js_instantiate, "instantiate", 2,
                                           JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, wa, "instantiateStreaming",
                      JS_NewCFunctionMagic(ctx, js_instantiate_streaming, "instantiateStreaming", 2,
                                           JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, wa, "compile",
                      JS_NewCFunctionMagic(ctx, js_compile, "compile", 1,
                                           JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, wa, "compileStreaming",
                      JS_NewCFunctionMagic(ctx, js_compile_streaming, "compileStreaming", 1,
                                           JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, wa, "validate",
                      JS_NewCFunctionMagic(ctx, js_validate, "validate", 1,
                                           JS_CFUNC_generic_magic, 0));

    JSValue runtime_err = make_error_class(ctx, global, "RuntimeError", "Error");
    if (!JS_IsUndefined(runtime_err)) JS_SetPropertyStr(ctx, wa, "RuntimeError", JS_DupValue(ctx, runtime_err));
    JSValue compile_err = make_error_class(ctx, global, "CompileError", "Error");
    if (!JS_IsUndefined(compile_err)) JS_SetPropertyStr(ctx, wa, "CompileError", JS_DupValue(ctx, compile_err));
    JSValue link_err = make_error_class(ctx, global, "LinkError", "Error");
    if (!JS_IsUndefined(link_err)) JS_SetPropertyStr(ctx, wa, "LinkError", JS_DupValue(ctx, link_err));

    JS_SetPropertyStr(ctx, global, "WebAssembly", wa);
    if (wasm_trace()) {
        fprintf(stderr, "wasm: WebAssembly installed\n");
        fflush(stderr);
    }
    return true;
}

void wasm_js_shutdown(JSContext* ctx) {
    (void)ctx;
    // The environment is process-wide and deliberately outlives contexts: the
    // browser keeps it for the next page load instead of re-parsing wasm3.
}

}  // namespace nefu
