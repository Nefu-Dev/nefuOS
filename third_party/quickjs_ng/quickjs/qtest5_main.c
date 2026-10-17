// qtest5: faithful nefuOS-style setup + SEH crash backtrace
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int interrupt_cb(JSRuntime *rt, void *opaque) { return 0; }
static char* readfile(const char* path, size_t* outsz) {
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1); size_t rd = fread(buf, 1, sz, f); fclose(f); buf[rd] = 0;
    *outsz = rd; return buf;
}
static JSValue stub(JSContext* c, JSValueConst thisv, int argc, JSValueConst* argv) {
    (void)c; (void)thisv; (void)argc; (void)argv; return JS_UNDEFINED;
}
static void setstr(JSContext* ctx, JSValue obj, const char* k, const char* v) {
    JSValue s = JS_NewString(ctx, v ? v : "");
    JS_SetPropertyStr(ctx, obj, k, s);
}
static int evalfile(JSContext* ctx, const char* path, const char* label) {
    size_t sz; char* buf = readfile(path, &sz);
    if (!buf) { fprintf(stderr, "cannot read %s\n", path); return 2; }
    fprintf(stderr, "eval %s (%zu bytes)\n", label, sz);
    int flags = JS_EVAL_TYPE_GLOBAL;
    if (getenv("NF_COMPILEONLY")) flags |= JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue ret = JS_Eval(ctx, buf, sz, label, flags);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx); const char* s = JS_ToCString(ctx, ex);
        fprintf(stderr, "  ERROR: %s\n", s ? s : "(null)");
        JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex); free(buf); return 1;
    }
    fprintf(stderr, "  OK\n"); JS_FreeValue(ctx, ret); free(buf); return 0;
}

static int g_step;
static JSRuntime* g_rt;
static int g_seh_written = 0;
static LONG WINAPI seh_filter(EXCEPTION_POINTERS* ep) {
    if (g_seh_written) return EXCEPTION_EXECUTE_HANDLER;  // avoid infinite re-fault loop
    g_seh_written = 1;
    FILE* f = fopen("D:/mycppos1/nefuOS/verify_out/qtest5_crash.txt", "w");
    if (f) {
        fprintf(f, "SEH code=0x%lX addr=0x%p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
        if (ep->ContextRecord) {
            CONTEXT* c = ep->ContextRecord;
            fprintf(f, "rip=%016llX rbx=%016llX rcx=%016llX rdx=%016llX rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX\n",
                (unsigned long long)c->Rip, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx,
                (unsigned long long)c->Rdx, (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
                (unsigned long long)c->Rbp, (unsigned long long)c->Rsp);
            // stack walk
            HMODULE modbase = GetModuleHandleA(NULL);
            unsigned char* lo = (unsigned char*)modbase;
            unsigned char* hi = lo + 0x800000;
            fprintf(f, "modbase=%p\n", modbase);
            unsigned long long* sp = (unsigned long long*)c->Rsp;
            for (int i = 0; i < 48; i++) {
                unsigned long long a = 0;
                if (!IsBadReadPtr(sp + i, sizeof(a))) a = *(sp + i);
                if (a >= (unsigned long long)lo && a < (unsigned long long)hi)
                    fprintf(f, "ra[%d]=0x%llX off=0x%llX\n", i, a, a - (unsigned long long)lo);
            }
        }
        fflush(f);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;  // terminate after dumping (no __except handler)
}

static void run(JSContext* ctx) {
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "window", JS_DupValue(ctx, g));
    JS_SetPropertyStr(ctx, g, "self", JS_DupValue(ctx, g));
    JS_SetPropertyStr(ctx, g, "innerWidth", JS_NewInt32(ctx, 1024));
    JS_SetPropertyStr(ctx, g, "innerHeight", JS_NewInt32(ctx, 768));
    JSValue nav = JS_NewObject(ctx);
    setstr(ctx, nav, "userAgent", "Mozilla/5.0 ... nefuOS/0.2");
    setstr(ctx, nav, "appName", "Netscape"); setstr(ctx, nav, "appVersion", "nefuOS 0.2");
    setstr(ctx, nav, "vendor", ""); setstr(ctx, nav, "platform", "Win32");
    setstr(ctx, nav, "language", "zh-CN"); setstr(ctx, nav, "languages", "zh-CN,en-US");
    JS_SetPropertyStr(ctx, nav, "onLine", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, nav, "hardwareConcurrency", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, nav, "maxTouchPoints", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, g, "navigator", nav);
    JSValue loc = JS_NewObject(ctx);
    setstr(ctx, loc, "href", "https://chat.deepseek.com/");
    setstr(ctx, loc, "protocol", "https:"); setstr(ctx, loc, "host", "chat.deepseek.com");
    setstr(ctx, loc, "hostname", "chat.deepseek.com"); setstr(ctx, loc, "port", "");
    setstr(ctx, loc, "origin", "https://chat.deepseek.com"); setstr(ctx, loc, "pathname", "/");
    setstr(ctx, loc, "search", ""); setstr(ctx, loc, "hash", "");
    JS_SetPropertyStr(ctx, g, "location", loc);
    const char* nfu[] = {"__nfuHttp","__nfuImg","__nfuCookieGet","__nfuCookieSet","__nfuStorageGet",
        "__nfuStorageSet","__nfuStorageRemove","__nfuStorageClear","__nfuStorageAll","__nfuNow",
        "__nfuLog","__nfuPumpNote","__nfuWsOpen","__nfuWsSend","__nfuWsPoll","__nfuWsClose","__nfuSync"};
    for (size_t i = 0; i < sizeof(nfu)/sizeof(nfu[0]); i++)
        JS_SetPropertyStr(ctx, g, nfu[i], JS_NewCFunction(ctx, stub, nfu[i], 4));
    JS_FreeValue(ctx, g);
    if (!getenv("NF_SKIP_SHIM"))
        evalfile(ctx, "C:/Users/huawei/AppData/Local/Temp/nefu_fix_test/shim.js", "<dom-shim>");
    JSValue g2 = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g2, "__nfuSeed",
        JS_NewString(ctx, "<div><div id=\"root\"></div><div><div id=\"root\"></div></div></div>"));
    JS_FreeValue(ctx, g2);
    if (!getenv("NF_SKIP_VENDORS")) {
        const char* vpath = getenv("NF_VENDORSPATH");
        evalfile(ctx, vpath ? vpath : "C:/Users/huawei/AppData/Local/Temp/nefu_fix_test/vendors_bundle.js", "<vendors>");
    }
    if (!getenv("NF_SKIP_MAIN")) {
        const char* mpath = getenv("NF_MAINPATH");
        evalfile(ctx, mpath ? mpath : "C:/Users/huawei/AppData/Local/Temp/nefu_fix_test/main_bundle.js", "<main>");
    }
    if (!getenv("NF_SKIP_SMALL")) {
        const char* small = "var probe = window.document; window.xyz = 1; probe;";
        JSValue ret = JS_Eval(ctx, small, strlen(small), "<probe>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ret)) {
            JSValue ex = JS_GetException(ctx); const char* s = JS_ToCString(ctx, ex);
            fprintf(stderr, "probe ERROR: %s\n", s ? s : "(null)");
            JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex);
        } else { fprintf(stderr, "probe OK\n"); JS_FreeValue(ctx, ret); }
    }
    JS_FreeContext(ctx); JS_FreeRuntime(g_rt);
}

int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(seh_filter);
    g_rt = JS_NewRuntime();
    JS_SetMemoryLimit(g_rt, 2ull * 1024 * 1024 * 1024);
    JS_SetInterruptHandler(g_rt, interrupt_cb, 0);
    JS_SetMaxStackSize(g_rt, 16 * 1024 * 1024);
    if (getenv("NF_NOGC"))
        JS_SetGCThreshold(g_rt, 0x7fffffffu);  // effectively disable auto GC
    JSContext* ctx = JS_NewContext(g_rt);
    run(ctx);
    return 0;
}
