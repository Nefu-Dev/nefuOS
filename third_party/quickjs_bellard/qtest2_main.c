// qtest2: reproduce nefuOS runtime conditions to isolate the crash
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int interrupt_cb(JSRuntime *rt, void *opaque) {
    // mimic nefuOS: allow but check a counter/time budget
    return 0; // 0 = continue
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: qtest2 <scriptfile> [memlimitMB]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open\n"); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char*)malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f); fclose(f); buf[rd] = 0;

    JSRuntime *rt = JS_NewRuntime();
    if (argc >= 3) JS_SetMemoryLimit(rt, atoi(argv[2]) * 1024 * 1024);
    JS_SetInterruptHandler(rt, interrupt_cb, 0);
    JS_SetMaxStackSize(rt, 16 * 1024 * 1024);
    JSContext *ctx = JS_NewContext(rt);
    JSValue ret = JS_Eval(ctx, buf, rd, "<test>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx);
        const char *s = JS_ToCString(ctx, ex);
        fprintf(stderr, "PARSE/EVAL ERROR: %s\n", s ? s : "(null)");
        JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex);
    } else {
        printf("OK eval success\n");
        JS_FreeValue(ctx, ret);
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt); free(buf);
    return 0;
}
