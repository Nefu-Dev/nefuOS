// qtest4: eval three scripts sequentially in one runtime (mirror nefuOS: shim, vendors, main)
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int interrupt_cb(JSRuntime *rt, void *opaque) { return 0; }

static char* readfile(const char* path, size_t* outsz) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f); fclose(f); buf[rd] = 0;
    *outsz = rd;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: qtest4 <file...>\n"); return 2; }
    JSRuntime* rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, 512 * 1024 * 1024);
    JS_SetInterruptHandler(rt, interrupt_cb, 0);
    JS_SetMaxStackSize(rt, 16 * 1024 * 1024);
    JSContext* ctx = JS_NewContext(rt);
    for (int i = 1; i < argc; i++) {
        size_t sz; char* buf = readfile(argv[i], &sz);
        if (!buf) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
        fprintf(stderr, "[%d] eval %s (%zu bytes)\n", i - 1, argv[i], sz);
        JSValue ret = JS_Eval(ctx, buf, sz, argv[i], JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ret)) {
            JSValue ex = JS_GetException(ctx);
            const char* s = JS_ToCString(ctx, ex);
            fprintf(stderr, "[%d] ERROR: %s\n", i - 1, s ? s : "(null)");
            JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex);
        } else {
            fprintf(stderr, "[%d] OK\n", i - 1);
            JS_FreeValue(ctx, ret);
        }
        free(buf);
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}
