# minijs.cpp: DOM shim bridge (html buffer + nativeSync + getGL + shim inject)
p = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s = open(p, encoding='utf-8').read()
n = 0

def rep(old, new, tag):
    global s, n
    c = s.count(old)
    if c != 1:
        print(f'MISS {tag} count={c}')
        return
    s = s.replace(old, new, 1)
    n += 1

# 1) include shim header
rep('''// minijs.h        // mini JS engine''',
    '''// minijs.h        // mini JS engine''', 'unused')
# find an include anchor near top
old = '#include "minijs.h"'
if old in s:
    s = s.replace(old, old + '\n#include "jsdom_shim.h"', 1)
    n += 1
else:
    print('MISS include anchor')

# 2) RunCtx html fields
rep('''    char* out;
    int out_sz;
    int oi;''',
    '''    char* out;
    int out_sz;
    int oi;
    char* html;      // DOM re-sync buffer (final serialized tree)
    int html_sz;
    int hi;''', 'runctx')

# 3) nativeSync C function + getGL entry (place before ensure_js)
rep('''// one shared runtime+context: global state survives across <script> blocks,
// exactly like a real browser''',
    '''// DOM shim bridge: JS serializes the node tree to an HTML string written
// into the html buffer (overwrites; the browser re-parses the final value).
static JSValue nfu_nativeSync(JSContext* c, JSValueConst thisv, int argc,
                              JSValueConst* argv) {
    RunCtx* rc = (RunCtx*)JS_GetContextOpaque(c);
    if (rc->html && rc->html_sz > 1) {
        rc->hi = 0;
        if (argc > 0 && JS_IsString(argv[0])) {
            const char* h = JS_ToCString(c, argv[0]);
            if (h) {
                size_t L = strlen(h);
                if (L >= (size_t)rc->html_sz - 1) L = (size_t)rc->html_sz - 1;
                memcpy(rc->html, h, L);
                rc->hi = (int)L;
                JS_FreeCString(c, h);
            }
        }
        rc->html[rc->hi] = 0;
    }
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
// exactly like a real browser''', 'bridge')

# 4) getContext: reuse make_gl_obj (replace the inline GL object creation)
old = '''    gl_emit(rc, "ctx", cid, "");
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
    JS_SetPropertyStr(c, g, "createTexture", JS_NewCFunction(c, nfu_createTexture, "createTexture", 0));'''
new = '''    gl_emit(rc, "ctx", cid, "");
    return make_gl_obj(c, cid);'''
rep(old, new, 'getcontext')

# 5) ensure_js: register nativeSync + __getGL + inject shim at the end
rep('''    JSValue g = JS_GetGlobalObject(s_ctx);
    JS_SetPropertyStr(s_ctx, g, "print",
        JS_NewCFunctionMagic(s_ctx, nfu_out, "print", 1, JS_CFUNC_generic_magic, 1));''',
    '''    JSValue g = JS_GetGlobalObject(s_ctx);
    JS_SetPropertyStr(s_ctx, g, "__nfuSync",
        JS_NewCFunction(s_ctx, nfu_nativeSync, "__nfuSync", 1));
    JS_SetPropertyStr(s_ctx, g, "__getGL",
        JS_NewCFunction(s_ctx, nfu_getGL, "__getGL", 1));
    JS_SetPropertyStr(s_ctx, g, "print",
        JS_NewCFunctionMagic(s_ctx, nfu_out, "print", 1, JS_CFUNC_generic_magic, 1));''', 'globals')

# inject the shim just before JS_FreeValue(s_ctx, g) at the end of ensure_js
rep('''    // keep the doc JSValue alive: freeing it here would drop the reference
    // below the one held by the global object and break document.write
    JS_FreeValue(s_ctx, g);
}''',
    '''    // keep the doc JSValue alive: freeing it here would drop the reference
    // below the one held by the global object and break document.write
    JS_FreeValue(s_ctx, g);
    // React/Vue-capable DOM shim: replaces document with a full node model
    JSValue shr = JS_Eval(s_ctx, JSDOM_SHIM_SRC, (size_t)strlen(JSDOM_SHIM_SRC),
                          "<dom-shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(shr)) {
        JSValue ex = JS_GetException(s_ctx);
        const char* m = JS_ToCString(s_ctx, ex);
        if (m) { fprintf(stderr, "jsdom shim error: %s\\n", m); JS_FreeCString(s_ctx, m); }
        JS_FreeValue(s_ctx, ex);
    }
    JS_FreeValue(s_ctx, shr);
}''', 'shim_inject')

# 6) mini_js_run signature + html buffer wiring
rep('''int mini_js_run(const char* script, char* out, int out_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    out[0] = 0;
    ensure_js();
    if (!s_ctx) return 1;
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    rc.deadline = platform_tick_ms() + 1000;   // 1s script budget''',
    '''int mini_js_run(const char* script, char* out, int out_sz,
                    char* html, int html_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    out[0] = 0;
    ensure_js();
    if (!s_ctx) return 1;
    RunCtx rc;
    rc.out = out; rc.out_sz = out_sz; rc.oi = 0;
    rc.html = html; rc.html_sz = html_sz; rc.hi = 0;
    if (html && html_sz > 0) html[0] = 0;
    rc.deadline = platform_tick_ms() + 1000;   // 1s script budget''', 'run_sig')

# 7) header signature
hp = r'D:\mycppos1\nefuOS\core\apps\minijs.h'
h = open(hp, encoding='utf-8').read()
h = h.replace('int mini_js_run(const char* script, char* out, int out_sz);',
              'int mini_js_run(const char* script, char* out, int out_sz,\n                   char* html, int html_sz);')
open(hp, 'w', encoding='utf-8', newline='').write(h)
print('header updated')

open(p, 'w', encoding='utf-8', newline='').write(s)
print(f'applied={n}')
