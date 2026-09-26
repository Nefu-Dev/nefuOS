// nefuOS software WebGL2 engine  - see swgl.h for design notes.
//
// Compiled for BOTH backends:
//  * bare kernel : -mgeneral-regs-only -> only single-precision soft-float;
//                  every literal is float, no double, no STL, no libc beyond
//                  the nefu::k* functions.
//  * host/test   : native float math (used by tools/swgl_test.cpp).
//
// The GLSL ES 3.00 interpreter is a pragmatic subset: scalars / vectors /
// matrices / sampler2D, uniforms / attributes / varyings / out, if / for /
// while, user functions, swizzles, indexing, constructors, the common math
// built-ins and texture()/textureLod(). Precision qualifiers, layout() and
// #version lines are parsed and ignored (all names resolve uniformly).
#include "swgl.h"
#ifdef SWGL_TEST_DEBUG
#include <cstdio>
#define SWDBG(...) do { FILE* dbg = fopen("D:/mycppos1/nefuOS/tools/swdbg.txt", "a"); if (dbg) { fprintf(dbg, __VA_ARGS__); fclose(dbg); } } while (0)
#else
#define SWDBG(...) do {} while (0)
#endif

namespace nefu {
namespace {

// bounded string copy (clip_str equivalent - browser.cpp's is TU-local)
static void sw_cp(char* dst, int cap, const char* src) {
    if (!dst || cap <= 0) return;
    int i = 0;
    if (src)
        while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}


// ===================== soft-float math (single precision only) =====================
static const float SW_PI   = 3.14159265358979323846f;
static const float SW_2PI  = 6.28318530717958647692f;
static const float SW_HALF = 1.57079632679489661923f;
static const float SW_LN2  = 0.69314718055994530942f;

static float sw_floorf(float x) {
    if (x > 1.0e9f) return 1.0e9f;
    if (x < -1.0e9f) return -1.0e9f;
    float r = (float)(int)x;
    if (x < r) r -= 1.0f;
    return r;
}
static float sw_ceilf(float x) { return -sw_floorf(-x); }
static float sw_fractf(float x) { return x - sw_floorf(x); }
static float sw_modf(float a, float b) { return b == 0.0f ? a : a - b * sw_floorf(a / b); }
static float sw_absf(float x) { return x < 0.0f ? -x : x; }
static float sw_minf(float a, float b) { return a < b ? a : b; }
static float sw_maxf(float a, float b) { return a > b ? a : b; }
static float sw_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static float sw_signf(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

static float sw_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x * 0.5f + 0.5f;
    for (int i = 0; i < 14; i++) g = 0.5f * (g + x / g);
    return g;
}
static float sw_rsqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    return 1.0f / sw_sqrtf(x);
}
static float sw_sinf(float x) {
    float r = sw_modf(x, SW_2PI);
    if (r > SW_PI) r -= SW_2PI;
    if (r < -SW_PI) r += SW_2PI;
    float r2 = r * r;
    return r * (1.0f + r2 * (-0.16666666667f + r2 * (0.00833333333f +
                r2 * (-0.000198412698f + r2 * 0.00000275573192f))));
}
static float sw_cosf(float x) { return sw_sinf(x + SW_HALF); }
static float sw_tanf(float x) {
    float c = sw_cosf(x);
    return c == 0.0f ? 0.0f : sw_sinf(x) / c;
}
static float sw_atanf(float z) {
    bool inv = sw_absf(z) > 1.0f;
    float a = inv ? 1.0f / z : z;
    float a2 = a * a;
    float r = a * (0.9998660f + a2 * (-0.3302995f + a2 * (0.1801410f +
              a2 * (-0.0851330f + a2 * 0.0208351f))));
    return inv ? SW_HALF - r : r;
}
static float sw_atan2f(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return SW_HALF;
        if (y < 0.0f) return -SW_HALF;
        return 0.0f;
    }
    float r = sw_atanf(y / x);
    if (x < 0.0f) r += (y >= 0.0f ? SW_PI : -SW_PI);
    return r;
}
static float sw_asin_safe(float x) {
    x = sw_clampf(x, -1.0f, 1.0f);
    return sw_atan2f(x, sw_sqrtf(sw_maxf(0.0f, 1.0f - x * x)));
}
static float sw_acos_safe(float x) { return SW_HALF - sw_asin_safe(x); }

static float sw_expf(float x) {
    float k = sw_floorf(x / SW_LN2);
    float r = x - k * SW_LN2;
    float p2 = 1.0f;
    int ik = (int)sw_clampf(k, -100.0f, 100.0f);
    if (ik >= -126 && ik <= 127) {
        uint32_t bits = (uint32_t)(ik + 127) << 23;
        __builtin_memcpy(&p2, &bits, 4);
    }
    float pr = 1.0f + r * (0.6931472f + r * (0.2402265f + r * (0.0555041f +
               r * (0.0096181f + r * 0.0013334f))));
    return p2 * pr;
}
static float sw_log2f(float x) {
    if (x <= 0.0f) return -1.0e30f;
    uint32_t bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = (int)((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFFu) | (127u << 23);
    float m;
    __builtin_memcpy(&m, &bits, 4);
    float u = m - 1.0f;
    float lm = u * (1.4426950f + u * (-0.7213475f + u * (0.4808984f +
               u * (-0.3606740f + u * 0.2886511f))));
    return (float)e + lm;
}
static float sw_logf(float x) { return sw_log2f(x) * SW_LN2; }
static float sw_exp2f(float x) { return sw_expf(x * SW_LN2); }
static float sw_powf(float a, float b) {
    if (a <= 0.0f) return 0.0f;
    if (b == 0.0f) return 1.0f;
    return sw_expf(b * sw_logf(a));
}

// ===================== GL constants =====================
enum {
    SW_VERTEX_SHADER = 0x8B31, SW_FRAGMENT_SHADER = 0x8B30,
    SW_ARRAY_BUFFER = 0x8892, SW_ELEMENT_ARRAY_BUFFER = 0x8893,
    SW_TRIANGLES = 0x0004, SW_TRIANGLE_STRIP = 0x0005, SW_TRIANGLE_FAN = 0x0006,
    SW_POINTS = 0x0000, SW_LINES = 0x0001, SW_LINE_STRIP = 0x0002, SW_LINE_LOOP = 0x0003,
    SW_COLOR_BUFFER_BIT = 0x4000, SW_DEPTH_BUFFER_BIT = 0x0100,
    SW_FLOAT = 0x1406, SW_UNSIGNED_BYTE = 0x1401, SW_UNSIGNED_SHORT = 0x1403, SW_UNSIGNED_INT = 0x1405,
    SW_BYTE = 0x1400, SW_SHORT = 0x1402,
    SW_DEPTH_TEST = 0x0B71, SW_BLEND = 0x0BE2, SW_CULL_FACE = 0x0B44, SW_SCISSOR_TEST = 0x0C11,
    SW_SRC_ALPHA = 0x0302, SW_ONE_MINUS_SRC_ALPHA = 0x0303, SW_ONE = 1, SW_ZERO = 0,
    SW_LESS = 0x0201, SW_LEQUAL = 0x0203, SW_GREATER = 0x0204, SW_GEQUAL = 0x0206,
    SW_EQUAL = 0x0202, SW_NOTEQUAL = 0x0205, SW_ALWAYS = 0x0207, SW_NEVER = 0x0200,
    SW_BACK = 0x0405, SW_FRONT = 0x0404, SW_CW = 0x0900, SW_CCW = 0x0901,
    SW_TEXTURE_2D = 0x0DE1, SW_TEXTURE0 = 0x84C0,
    SW_RGBA = 0x1908, SW_RGB = 0x1907, SW_LUMINANCE = 0x1909, SW_ALPHA = 0x1906,
    SW_LUMINANCE_ALPHA = 0x190A,
    SW_TEXTURE_MIN_FILTER = 0x2801, SW_TEXTURE_MAG_FILTER = 0x2800,
    SW_TEXTURE_WRAP_S = 0x2802, SW_TEXTURE_WRAP_T = 0x2803,
    SW_NEAREST = 0x2600, SW_LINEAR = 0x2601, SW_CLAMP_TO_EDGE = 0x812F,
    SW_REPEAT = 0x2901, SW_MIRRORED_REPEAT = 0x8370,
    SW_UNPACK_ALIGNMENT = 0x0CF5, SW_UNPACK_FLIP_Y = 0x9240,
    SW_MAX_ATTRIBS = 16, SW_MAX_UNIFORMS = 256, SW_MAX_TEX = 16, SW_MAX_PROG = 8,
    SW_MAX_SH = 16, SW_MAX_BUF = 32, SW_MAX_VAO = 8, SW_MAX_LOCAL = 64, SW_MAX_FUNC = 24,
    SW_MAX_ARRAY = 16, SW_MAX_FRAGV = 16
};

// ===================== value type =====================
enum SwType : int {
    T_VOID = 0, T_F, T_I, T_B, T_V2, T_V3, T_V4, T_M2, T_M3, T_M4, T_SAMP
};
static int sw_tsize(SwType t) {
    switch (t) {
        case T_M2: return 4;
        case T_M3: return 9;
        case T_M4: return 16;
        case T_V2: return 2;
        case T_V3: return 3;
        case T_V4: return 4;
        default: return 1;
    }
}
struct SwVal {
    SwType t;
    int array_len;   // >1 for arrays (storage = array_len * sw_tsize(t))
    float f[16];
};
static SwVal sw_val(SwType t) {
    SwVal v;
    v.t = t;
    v.array_len = 1;
    for (int i = 0; i < 16; i++) v.f[i] = 0.0f;
    return v;
}

// mix/step/smoothstep (component-wise; no captured lambdas)
static SwVal sw_mix(const SwVal& a, const SwVal& b, const SwVal& t) {
    bool av = a.t >= T_V2 && a.t <= T_V4;
    bool bv = b.t >= T_V2 && b.t <= T_V4;
    bool tv = t.t >= T_V2 && t.t <= T_V4;
    int n = av ? sw_tsize(a.t) : (bv ? sw_tsize(b.t) : (tv ? sw_tsize(t.t) : 1));
    SwVal r = av ? a : sw_val(T_F); if (!av) r.f[0] = a.f[0];
    for (int i = 0; i < n; i++) {
        float x = av ? a.f[i] : a.f[0];
        float y = bv ? b.f[i] : b.f[0];
        float w = tv ? t.f[i] : t.f[0];
        r.f[i] = x * (1.0f - w) + y * w;
    }
    return r;
}
static SwVal sw_step(const SwVal& e, const SwVal& x) {
    bool ev = e.t >= T_V2 && e.t <= T_V4;
    bool xv = x.t >= T_V2 && x.t <= T_V4;
    int n = ev ? sw_tsize(e.t) : (xv ? sw_tsize(x.t) : 1);
    SwVal r = ev ? e : (xv ? x : sw_val(T_F)); if (!ev && !xv) r.f[0] = e.f[0];
    for (int i = 0; i < n; i++) {
        float ei = ev ? e.f[i] : e.f[0];
        float xi = xv ? x.f[i] : x.f[0];
        r.f[i] = xi < ei ? 0.0f : 1.0f;
    }
    return r;
}
static SwVal sw_smooth(const SwVal& e0, const SwVal& e1, const SwVal& x) {
    bool a0 = e0.t >= T_V2 && e0.t <= T_V4;
    bool a1 = e1.t >= T_V2 && e1.t <= T_V4;
    bool xv = x.t >= T_V2 && x.t <= T_V4;
    int n = a0 ? sw_tsize(e0.t) : (a1 ? sw_tsize(e1.t) : (xv ? sw_tsize(x.t) : 1));
    SwVal r = a0 ? e0 : (xv ? x : sw_val(T_F)); if (!a0 && !xv) r.f[0] = e0.f[0];
    for (int i = 0; i < n; i++) {
        float a = a0 ? e0.f[i] : e0.f[0];
        float b = a1 ? e1.f[i] : e1.f[0];
        float v = xv ? x.f[i] : x.f[0];
        float t = sw_clampf((v - a) / (b - a), 0.0f, 1.0f);
        r.f[i] = t * t * (3.0f - 2.0f * t);
    }
    return r;
}

// ===================== AST =====================
enum SwN : int {
    N_EMPTY = 0, N_INT, N_FLOAT, N_IDENT, N_BIN, N_UN, N_CALL, N_INDEX, N_SWIZ,
    N_TERN, N_ASSIGN, N_DECL, N_BLOCK, N_IF, N_FOR, N_WHILE, N_RET, N_DISCARD,
    N_BREAK, N_CONT, N_EXPR, N_FUNCDEF, N_PARAM
};
enum SwOp : int {
    OP_ADD = 1, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_NOT, OP_BNOT, OP_POS,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_AND, OP_OR, OP_BAND, OP_BOR,
    OP_BXOR, OP_ASSIGN, OP_AADD, OP_ASUB, OP_AMUL, OP_ADIV, OP_AMOD, OP_TERN,
    OP_CALL
};
struct SwNode {
    int k;
    int op;
    intptr_t a, b, c, d;   // child node pointers (raw, arena-lifetime)
    float fv;
    char name[20];
    int swz[4];
    int swz_n;
};
struct SwArena {
    SwNode* nodes;
    int cap, used;
    SwArena* next;
};
static SwNode* sw_anew(SwArena*& ar) {
    if (!ar || ar->used >= ar->cap) {
        SwArena* n = (SwArena*)kalloc(sizeof(SwArena));
        if (!n) return 0;
        n->nodes = (SwNode*)kalloc(sizeof(SwNode) * 1024);
        if (!n->nodes) { kfree(n); return 0; }
        n->cap = 1024;
        n->used = 0;
        n->next = ar;
        ar = n;
    }
    SwNode* nd = &ar->nodes[ar->used++];
    memset(nd, 0, sizeof(SwNode));
    nd->k = N_EMPTY;
    return nd;
}
static void sw_afree(SwArena*& ar) {
    while (ar) {
        SwArena* n = ar->next;
        if (ar->nodes) kfree(ar->nodes);
        kfree(ar);
        ar = n;
    }
}

// ===================== tokenizer =====================
enum SwTok : int { TK_EOF = 0, TK_ID, TK_INT, TK_FLOAT, TK_OP };
struct SwLexer {
    const char* p;
    int k;
    char txt[24];
    float fv;
};
static bool sw_isid(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool sw_isnum(char c) { return c >= '0' && c <= '9'; }

static void sw_lex_next(SwLexer& lx) {
    const char*& p = lx.p;
    lx.k = TK_EOF;
    lx.txt[0] = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        break;
    }
    if (!*p) return;
    if (*p == '#') { while (*p && *p != '\n') p++; sw_lex_next(lx); return; }
    if (sw_isid(*p)) {
        int n = 0;
        while (sw_isid(*p) || sw_isnum(*p)) { if (n < 19) lx.txt[n++] = *p; p++; }
        lx.txt[n] = 0;
        lx.k = TK_ID;
        return;
    }
    if (sw_isnum(*p) || (*p == '.' && sw_isnum(p[1]))) {
        char b[40];
        int n = 0;
        bool isf = false;
        while (sw_isnum(*p) || *p == '.' || *p == 'e' || *p == 'E' ||
               ((*p == '+' || *p == '-') && n > 0 && (b[n - 1] == 'e' || b[n - 1] == 'E'))) {
            if (*p == '.' || *p == 'e' || *p == 'E') isf = true;
            if (n < 38) b[n++] = *p;
            p++;
        }
        b[n] = 0;
        lx.k = isf ? TK_FLOAT : TK_INT;
        lx.fv = sw_atof(b);
        return;
    }
    static const char* ops2[] = { "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "++", "--", "*=", "/=", "%=", 0 };
    for (int i = 0; ops2[i]; i++)
        if (p[0] == ops2[i][0] && p[1] == ops2[i][1]) {
            lx.txt[0] = ops2[i][0];
            lx.txt[1] = ops2[i][1];
            lx.txt[2] = 0;
            p += 2;
            lx.k = TK_OP;
            return;
        }
    lx.k = TK_OP;
    lx.txt[0] = *p;
    lx.txt[1] = 0;
    p++;
}

static SwType sw_type_of_name(const char* s) {
    if (!strcmp(s, "float")) return T_F;
    if (!strcmp(s, "int")) return T_I;
    if (!strcmp(s, "bool")) return T_B;
    if (!strcmp(s, "vec2")) return T_V2;
    if (!strcmp(s, "vec3")) return T_V3;
    if (!strcmp(s, "vec4")) return T_V4;
    if (!strcmp(s, "mat2")) return T_M2;
    if (!strcmp(s, "mat3")) return T_M3;
    if (!strcmp(s, "mat4")) return T_M4;
    if (!strcmp(s, "sampler2D")) return T_SAMP;
    return T_VOID;
}

// ===================== shader / program / canvas data =====================
struct SwVar {
    char name[20];
    SwType t;
    int array_len;
    SwVal val;
    bool is_uniform, is_attr, is_varying, is_out, is_const;
};
struct SwShader {
    bool used;
    unsigned sid;
    int type;
    SwArena* arena;
    SwNode* funcs[SW_MAX_FUNC];
    int funcs_n;
    int main_idx;
    SwVar vars[SW_MAX_UNIFORMS];
    int vars_n;
    char err[160];
    bool compiled;
};
struct SwProg {
    bool used;
    unsigned sid;
    int vs, fs;
    char uniform_name[SW_MAX_UNIFORMS][20];
    int uniform_vs[SW_MAX_UNIFORMS];
    int uniform_fs[SW_MAX_UNIFORMS];
    int uniform_n;
    char attr_name[SW_MAX_UNIFORMS][20];
    int attr_var[SW_MAX_UNIFORMS];     // VS var index
    int attr_slot[SW_MAX_UNIFORMS];    // global attribute slot 0..15
    int attr_n;
    char varying_fs_name[SW_MAX_FRAGV][20];
    int varying_fs_var[SW_MAX_FRAGV];   // FS var index
    int varying_vs_var[SW_MAX_FRAGV];   // VS var index (by name match)
    int varying_n;
    char err[160];
    bool linked;
};
struct SwBuf_ {
    bool used;
    unsigned sid;
    int target;
    uint8_t* data;
    int bytes;
};
struct SwVao {
    bool used;
    unsigned sid;
    struct SwAttrib {
        bool enabled;
        int buf;      // canvas buffer index (-1 none)
        int size;
        int type;
        int stride;
        int offset;
        int divisor;
    } attrs[SW_MAX_ATTRIBS];
    int elem_buf;
};
struct SwTex {
    bool used;
    unsigned sid;
    int target;
    int w, h;
    int internal, format, data_type;
    uint8_t* rgba;   // RGBA8 top-down
    int min_filter, mag_filter, wrap_s, wrap_t;
    bool flip_y;
};
struct SwCanvas {
    bool used;
    char cid[32];
    int w, h;
    uint8_t* fb;
    float* depth;
    float cr, cg, cb, ca;
    SwShader sh[SW_MAX_SH];
    SwProg pr[SW_MAX_PROG];
    SwBuf_ bu[SW_MAX_BUF];
    SwVao va[SW_MAX_VAO];
    SwTex tx[SW_MAX_TEX];
    int cur_prog;    // canvas prog index (-1)
    int cur_buf;     // ARRAY_BUFFER binding (canvas buf index, -1)
    int cur_elem;    // ELEMENT_ARRAY_BUFFER binding
    int cur_vao;     // canvas VAO index (-1 = default VAO)
    SwVao def_vao;   // the default vertex array (used when cur_vao == -1)
    int active_unit;
    int unit_tex[8];
    unsigned u_loc[256];   // proxy uniform id -> program uniform slot + 1
    unsigned a_loc[256];   // proxy attrib id -> global slot + 1
    // state
    bool depth_test, blend, cull, scissor;
    int depth_func, blend_src, blend_dst, cull_face;
    int sc_x, sc_y, sc_w, sc_h;
    bool color_mask[4];
    bool depth_mask;
    int unpack_alignment, unpack_flip_y;
    // draw-time scratch (set before shader exec)
    SwVal attr_scratch[SW_MAX_UNIFORMS];
    SwVal vs_varying[SW_MAX_FRAGV];
    SwVal frag_varying[SW_MAX_FRAGV];
    SwVal gl_fragcoord;
    float fragcolor[4];
    float fragdepth;
    float gl_pos[4];
    float gl_psize;
    bool discard;
};
static SwCanvas s_cv[4];

// ===================== parser =====================
struct SwParser {
    SwLexer lx;
    SwShader* sh;
    SwArena* arena;
    bool err;
};
static void sw_parse_err(SwParser& ps, const char* msg) {
    if (!ps.err) {
    if (ps.lx.p) { char ctx[64]; int m = 0; const char* q = ps.lx.p; while (*q && *q != 10 && m < 63) { ctx[m++] = *q; q++; } ctx[m] = 0; SWDBG("  parse_err at: %s\n", ctx); }
        ps.err = true;
        if (ps.sh) sw_cp(ps.sh->err, sizeof(ps.sh->err), msg);
    }
}
static bool sw_tok_is(SwLexer& lx, const char* s) {
    return lx.k == TK_OP && strcmp(lx.txt, s) == 0;
}
static bool sw_tok_key(SwLexer& lx, const char* s) {
    return lx.k == TK_ID && strcmp(lx.txt, s) == 0;
}

static SwNode* sw_parse_expr(SwParser& ps, int minprec);
static SwNode* sw_parse_stmt(SwParser& ps);

// block chain: each N_BLOCK holds one stmt in a and chains via b
static void sw_block_add(SwParser& ps, SwNode*& head, SwNode* stmt) {
    if (!head) {
        head = sw_anew(ps.arena);
        if (!head) return;
        head->k = N_BLOCK;
        head->a = (intptr_t)stmt;
        return;
    }
    SwNode* t = head;
    while (t->b) t = (SwNode*)t->b;
    SwNode* n = sw_anew(ps.arena);
    if (!n) return;
    n->k = N_BLOCK;
    n->a = (intptr_t)stmt;
    t->b = (intptr_t)n;
}
static void sw_set_child(SwNode* parent, int which, SwNode* child) {
    intptr_t* slot = which == 0 ? &parent->a : (which == 1 ? &parent->b : (which == 2 ? &parent->c : &parent->d));
    *slot = child ? (intptr_t)child : 0;
}

static SwNode* sw_parse_type_node(SwParser& ps) {
    SwType t = sw_type_of_name(ps.lx.txt);
    if (t == T_VOID && !sw_tok_key(ps.lx, "void")) {
        sw_parse_err(ps, "unknown type");
        return 0;
    }
    sw_lex_next(ps.lx);
    SwNode* n = sw_anew(ps.arena);
    if (!n) return 0;
    n->k = N_EMPTY;
    n->op = (int)t;
    return n;
}

static SwNode* sw_parse_primary(SwParser& ps) {
    SwLexer& lx = ps.lx;
    SwNode* n = 0;
    if (lx.k == TK_INT) {
        n = sw_anew(ps.arena);
        if (n) { n->k = N_INT; n->fv = (float)lx.fv; }
        sw_lex_next(lx);
        return n;
    }
    if (lx.k == TK_FLOAT) {
        n = sw_anew(ps.arena);
        if (n) { n->k = N_FLOAT; n->fv = lx.fv; }
        sw_lex_next(lx);
        return n;
    }
    if (lx.k == TK_ID) {
        if (sw_tok_key(lx, "true") || sw_tok_key(lx, "false")) {
            n = sw_anew(ps.arena);
            if (n) { n->k = N_FLOAT; n->fv = sw_tok_key(lx, "true") ? 1.0f : 0.0f; }
            sw_lex_next(lx);
            return n;
        }
        SwType ct = sw_type_of_name(lx.txt);
        if (ct != T_VOID) {
            sw_lex_next(lx);
            if (lx.k == TK_OP && lx.txt[0] == '(') {
                sw_lex_next(lx);
                n = sw_anew(ps.arena);
                if (n) {
                    n->k = N_CALL;
                    n->op = (int)ct;      // ctor type
                    sw_cp(n->name, sizeof(n->name), "ctor");
                }
                SwNode* args[4] = { 0, 0, 0, 0 };
                int an = 0;
                if (!(lx.k == TK_OP && lx.txt[0] == ')')) {
                    for (;;) {
                        SwNode* e = sw_parse_expr(ps, 0);
                        if (an < 4) args[an] = e;
                        an++;
                        if (lx.k == TK_OP && lx.txt[0] == ',') { sw_lex_next(lx); continue; }
                        break;
                    }
                }
                sw_set_child(n, 0, args[0]);
                sw_set_child(n, 1, args[1]);
                sw_set_child(n, 2, args[2]);
                sw_set_child(n, 3, args[3]);
                if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
                else sw_lex_next(lx);
                return n;
            }
            sw_parse_err(ps, "bad ctor");
            return n;
        }
        n = sw_anew(ps.arena);
        if (n) {
            n->k = N_IDENT;
            sw_cp(n->name, sizeof(n->name), lx.txt);
        }
        sw_lex_next(lx);
        return n;
    }
    if (lx.k == TK_OP && lx.txt[0] == '(') {
        sw_lex_next(lx);
        n = sw_parse_expr(ps, 0);
        if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
        else sw_lex_next(lx);
        return n;
    }
    if (lx.k == TK_OP && lx.txt[0] == '-') {
        sw_lex_next(lx);
        n = sw_anew(ps.arena);
        if (n) { n->k = N_UN; n->op = OP_NEG; sw_set_child(n, 0, sw_parse_expr(ps, 11)); }
        return n;
    }
    if (lx.k == TK_OP && lx.txt[0] == '!') {
        sw_lex_next(lx);
        n = sw_anew(ps.arena);
        if (n) { n->k = N_UN; n->op = OP_NOT; sw_set_child(n, 0, sw_parse_expr(ps, 11)); }
        return n;
    }
    if (lx.k == TK_OP && lx.txt[0] == '~') {
        sw_lex_next(lx);
        n = sw_anew(ps.arena);
        if (n) { n->k = N_UN; n->op = OP_BNOT; sw_set_child(n, 0, sw_parse_expr(ps, 11)); }
        return n;
    }
    if (lx.k == TK_OP && lx.txt[0] == '+') {
        sw_lex_next(lx);
        n = sw_anew(ps.arena);
        if (n) { n->k = N_UN; n->op = OP_POS; sw_set_child(n, 0, sw_parse_expr(ps, 11)); }
        return n;
    }
    SWDBG("unexpected token at: %s\n", lx.txt);
    sw_parse_err(ps, "unexpected token");
    sw_lex_next(lx);
    return sw_anew(ps.arena);
}

static SwNode* sw_parse_postfix(SwParser& ps) {
    SwNode* n = sw_parse_primary(ps);
    if (!n) return 0;
    SwLexer& lx = ps.lx;
    for (;;) {
        if (lx.k == TK_OP && lx.txt[0] == '(') {
            sw_lex_next(lx);
            SwNode* call = sw_anew(ps.arena);
            if (!call) return 0;
            call->k = N_CALL;
            call->op = OP_CALL;
            sw_cp(call->name, sizeof(call->name), n->name);
            SwNode* args[4] = { 0, 0, 0, 0 };
            int an = 0;
            if (!(lx.k == TK_OP && lx.txt[0] == ')')) {
                for (;;) {
                    SwNode* e = sw_parse_expr(ps, 0);
                    if (an < 4) args[an] = e;
                    an++;
                    if (lx.k == TK_OP && lx.txt[0] == ',') { sw_lex_next(lx); continue; }
                    break;
                }
            }
            sw_set_child(call, 0, args[0]);
            sw_set_child(call, 1, args[1]);
            sw_set_child(call, 2, args[2]);
            sw_set_child(call, 3, args[3]);
            if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
            else sw_lex_next(lx);
            n = call;
            continue;
        }
        if (lx.k == TK_OP && lx.txt[0] == '[') {
            sw_lex_next(lx);
            SwNode* idx = sw_parse_expr(ps, 0);
            if (!(lx.k == TK_OP && lx.txt[0] == ']')) sw_parse_err(ps, "expected ]");
            else sw_lex_next(lx);
            SwNode* in = sw_anew(ps.arena);
            if (in) {
                in->k = N_INDEX;
                sw_set_child(in, 0, n);
                sw_set_child(in, 1, idx);
            }
            n = in;
            continue;
        }
        if (lx.k == TK_OP && lx.txt[0] == '.') {
            sw_lex_next(lx);
            SwNode* sw = sw_anew(ps.arena);
            if (sw) {
                sw->k = N_SWIZ;
                sw_set_child(sw, 0, n);
                sw->swz_n = 0;
                if (lx.k == TK_ID) {
                    const char* sc = lx.txt;
                    for (int ci = 0; sc[ci] && sw->swz_n < 4; ci++) {
                        char c = sc[ci];
                        int comp = -1;
                        if (c == 'x' || c == 'r' || c == 's') comp = 0;
                        else if (c == 'y' || c == 'g' || c == 't') comp = 1;
                        else if (c == 'z' || c == 'b' || c == 'p') comp = 2;
                        else if (c == 'w' || c == 'a' || c == 'q') comp = 3;
                        if (comp < 0) break;
                        sw->swz[sw->swz_n++] = comp;
                    }
                    sw_lex_next(lx);
                }
                if (sw->swz_n == 0) sw_parse_err(ps, "bad swizzle");
            }
            n = sw;
            continue;
        }
        if (lx.k == TK_OP && (lx.txt[0] == '+' || lx.txt[0] == '-') && lx.txt[1] == lx.txt[0]) {
            char opc = lx.txt[0];
            sw_lex_next(lx);
            SwNode* dup = sw_anew(ps.arena);
            SwNode* one = sw_anew(ps.arena);
            SwNode* rhs = sw_anew(ps.arena);
            SwNode* asn = sw_anew(ps.arena);
            if (dup && one && rhs && asn) {
                *dup = *n;
                one->k = N_FLOAT; one->fv = 1.0f;
                rhs->k = N_BIN;
                rhs->op = opc == '+' ? OP_ADD : OP_SUB;
                sw_set_child(rhs, 0, dup);
                sw_set_child(rhs, 1, one);
                asn->k = N_ASSIGN;
                asn->op = OP_ASSIGN;
                sw_set_child(asn, 0, n);
                sw_set_child(asn, 1, rhs);
                n = asn;
            }
            continue;
        }
        break;
    }
    return n;
}

static int sw_prec_of(const char* op) {
    if (!strcmp(op, "?")) return 0;
    if (!strcmp(op, "||")) return 1;
    if (!strcmp(op, "&&")) return 2;
    if (!strcmp(op, "|")) return 3;
    if (!strcmp(op, "^")) return 4;
    if (!strcmp(op, "&")) return 5;
    if (!strcmp(op, "==") || !strcmp(op, "!=")) return 6;
    if (!strcmp(op, "<") || !strcmp(op, "<=") || !strcmp(op, ">") || !strcmp(op, ">=")) return 7;
    if (!strcmp(op, "+") || !strcmp(op, "-")) return 8;
    if (!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) return 9;
    return -1;
}
static int sw_op_code(const char* op) {
    if (!strcmp(op, "+")) return OP_ADD;
    if (!strcmp(op, "-")) return OP_SUB;
    if (!strcmp(op, "*")) return OP_MUL;
    if (!strcmp(op, "/")) return OP_DIV;
    if (!strcmp(op, "%")) return OP_MOD;
    if (!strcmp(op, "==")) return OP_EQ;
    if (!strcmp(op, "!=")) return OP_NE;
    if (!strcmp(op, "<")) return OP_LT;
    if (!strcmp(op, "<=")) return OP_LE;
    if (!strcmp(op, ">")) return OP_GT;
    if (!strcmp(op, ">=")) return OP_GE;
    if (!strcmp(op, "&&")) return OP_AND;
    if (!strcmp(op, "||")) return OP_OR;
    if (!strcmp(op, "&")) return OP_BAND;
    if (!strcmp(op, "|")) return OP_BOR;
    if (!strcmp(op, "^")) return OP_BXOR;
    return 0;
}
static int sw_assign_op(const char* op) {
    if (!strcmp(op, "=")) return OP_ASSIGN;
    if (!strcmp(op, "+=")) return OP_AADD;
    if (!strcmp(op, "-=")) return OP_ASUB;
    if (!strcmp(op, "*=")) return OP_AMUL;
    if (!strcmp(op, "/=")) return OP_ADIV;
    if (!strcmp(op, "%=")) return OP_AMOD;
    return 0;
}

static SwNode* sw_parse_expr(SwParser& ps, int minprec) {
    SwNode* left = sw_parse_postfix(ps);
    if (!left) return 0;
    SwLexer& lx = ps.lx;
    for (;;) {
        if (minprec <= 0 && lx.k == TK_OP && lx.txt[0] == '?') {
            sw_lex_next(lx);
            SwNode* t = sw_parse_expr(ps, 0);
            if (!(lx.k == TK_OP && lx.txt[0] == ':')) sw_parse_err(ps, "expected :");
            else sw_lex_next(lx);
            SwNode* f = sw_parse_expr(ps, 0);
            SwNode* tern = sw_anew(ps.arena);
            if (tern) {
                tern->k = N_TERN;
                sw_set_child(tern, 0, left);
                sw_set_child(tern, 1, t);
                sw_set_child(tern, 2, f);
            }
            left = tern;
            continue;
        }
        if (lx.k == TK_OP) {
            int ao = sw_assign_op(lx.txt);
            if (ao && minprec <= 0) {
                sw_lex_next(lx);
                SwNode* r = sw_parse_expr(ps, 0);
                SwNode* as = sw_anew(ps.arena);
                if (as) {
                    as->k = N_ASSIGN;
                    as->op = ao;
                    sw_set_child(as, 0, left);
                    sw_set_child(as, 1, r);
                }
                left = as;
                continue;
            }
            int p = sw_prec_of(lx.txt);
            if (p < 0 || p < minprec) break;
            int op = sw_op_code(lx.txt);
            sw_lex_next(lx);
            SwNode* right = sw_parse_expr(ps, p + 1);
            SwNode* bin = sw_anew(ps.arena);
            if (bin) {
                bin->k = N_BIN;
                bin->op = op;
                sw_set_child(bin, 0, left);
                sw_set_child(bin, 1, right);
            }
            left = bin;
            continue;
        }
        break;
    }
    return left;
}

// "type name [= expr]" (optional [N])
static SwNode* sw_parse_decl_tail(SwParser& ps, SwType t, const char* name) {
    SwLexer& lx = ps.lx;
    SwNode* d = sw_anew(ps.arena);
    if (!d) return 0;
    d->k = N_DECL;
    d->op = (int)t;
    sw_cp(d->name, sizeof(d->name), name);
    d->a = d->b = d->c = d->d = -1;
    if (lx.k == TK_OP && lx.txt[0] == '[') {
        sw_lex_next(lx);
        int alen = 1;
        if (lx.k == TK_INT) { alen = (int)lx.fv; sw_lex_next(lx); }
        if (alen < 1) alen = 1;
        if (alen > SW_MAX_ARRAY) alen = SW_MAX_ARRAY;
        d->d = alen;
        if (!(lx.k == TK_OP && lx.txt[0] == ']')) sw_parse_err(ps, "expected ]");
        else sw_lex_next(lx);
    }
    if (lx.k == TK_OP && lx.txt[0] == '=') {
        sw_lex_next(lx);
        sw_set_child(d, 0, sw_parse_expr(ps, 0));
    }
    return d;
}

static void sw_add_global(SwShader& sh, const char* name, SwType t, int alen,
                          bool uni, bool attr, bool vary, bool out, bool is_const) {
    for (int i = 0; i < sh.vars_n; i++)
        if (!strcmp(sh.vars[i].name, name)) return;
    if (sh.vars_n >= SW_MAX_UNIFORMS) return;
    SwVar& v = sh.vars[sh.vars_n++];
    sw_cp(v.name, sizeof(v.name), name);
    v.t = t;
    v.array_len = alen > 1 ? alen : 1;
    v.val = sw_val(t);
    v.val.array_len = v.array_len;
    v.is_uniform = uni;
    v.is_attr = attr;
    v.is_varying = vary;
    v.is_out = out;
    v.is_const = is_const;
}

static SwNode* sw_parse_function(SwParser& ps, SwType ret, const char* fname) {
    SwLexer& lx = ps.lx;
    SwNode* fn = sw_anew(ps.arena);
    if (!fn) return 0;
    fn->k = N_FUNCDEF;
    fn->op = (int)ret;
    sw_cp(fn->name, sizeof(fn->name), fname);
    if (!(lx.k == TK_OP && lx.txt[0] == '(')) { sw_parse_err(ps, "expected ("); return fn; }
    sw_lex_next(lx);
    SwNode* params = 0;
    if (!(lx.k == TK_OP && lx.txt[0] == ')')) {
        for (;;) {
            while (sw_tok_key(lx, "in") || sw_tok_key(lx, "out") || sw_tok_key(lx, "inout") ||
                   sw_tok_key(lx, "const") || sw_tok_key(lx, "highp") || sw_tok_key(lx, "mediump") ||
                   sw_tok_key(lx, "lowp")) sw_lex_next(lx);
            SwNode* tn = sw_parse_type_node(ps);
            SwType pt = tn ? (SwType)tn->op : T_VOID;
            if (lx.k == TK_ID) {
                SwNode* p = sw_anew(ps.arena);
                if (p) {
                    p->k = N_PARAM;
                    p->op = (int)pt;
                    sw_cp(p->name, sizeof(p->name), lx.txt);
                    sw_block_add(ps, params, p);
                }
                sw_lex_next(lx);
            } else sw_parse_err(ps, "param name");
            if (lx.k == TK_OP && lx.txt[0] == ',') { sw_lex_next(lx); continue; }
            break;
        }
    }
    if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
    else sw_lex_next(lx);
    if (!(lx.k == TK_OP && lx.txt[0] == '{')) { sw_parse_err(ps, "expected {"); return fn; }
    sw_lex_next(lx);
    SwNode* body = 0;
    while (!(lx.k == TK_OP && lx.txt[0] == '}')) {
        if (lx.k == TK_EOF) { sw_parse_err(ps, "unterminated function"); break; }
        sw_block_add(ps, body, sw_parse_stmt(ps));
    }
    sw_lex_next(lx);
    sw_set_child(fn, 0, params);
    sw_set_child(fn, 1, body);
    return fn;
}

static SwNode* sw_parse_stmt(SwParser& ps) {
    SwLexer& lx = ps.lx;
    SWDBG("stmt enter tok=%s\n", lx.txt);
    if (lx.k == TK_OP && lx.txt[0] == '{') {
        sw_lex_next(lx);
        SwNode* blk = 0;
        while (!(lx.k == TK_OP && lx.txt[0] == '}')) {
            if (lx.k == TK_EOF) { sw_parse_err(ps, "unterminated block"); break; }
            sw_block_add(ps, blk, sw_parse_stmt(ps));
        }
        sw_lex_next(lx);
        return blk;
    }
    if (sw_tok_key(lx, "if")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == '(')) { sw_parse_err(ps, "expected ("); return 0; }
        sw_lex_next(lx);
        SwNode* cond = sw_parse_expr(ps, 0);
        if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
        else sw_lex_next(lx);
        SwNode* t = sw_parse_stmt(ps);
        SwNode* f = 0;
        if (sw_tok_key(lx, "else")) {
            sw_lex_next(lx);
            f = sw_parse_stmt(ps);
        }
        SwNode* n = sw_anew(ps.arena);
        if (n) {
            n->k = N_IF;
            sw_set_child(n, 0, cond);
            sw_set_child(n, 1, t);
            sw_set_child(n, 2, f);
        }
        return n;
    }
    if (sw_tok_key(lx, "for")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == '(')) { sw_parse_err(ps, "expected ("); return 0; }
        sw_lex_next(lx);
        SwNode* init = 0;
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) {
            SwType t = sw_type_of_name(lx.txt);
            if (t != T_VOID) {
                sw_lex_next(lx);
                if (lx.k == TK_ID) {
                    char nm[20]; sw_cp(nm, sizeof(nm), lx.txt);
                    sw_lex_next(lx);
                    init = sw_parse_decl_tail(ps, t, nm);
                }
            } else init = sw_parse_expr(ps, 0);
        }
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        SwNode* cond = 0;
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) cond = sw_parse_expr(ps, 0);
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        SwNode* upd = 0;
        if (!(lx.k == TK_OP && lx.txt[0] == ')')) upd = sw_parse_expr(ps, 0);
        if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
        else sw_lex_next(lx);
        SwNode* body = sw_parse_stmt(ps);
        SwNode* n = sw_anew(ps.arena);
        if (n) {
            n->k = N_FOR;
            sw_set_child(n, 0, init);
            sw_set_child(n, 1, cond);
            sw_set_child(n, 2, upd);
            sw_set_child(n, 3, body);
        }
        return n;
    }
    if (sw_tok_key(lx, "while")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == '(')) { sw_parse_err(ps, "expected ("); return 0; }
        sw_lex_next(lx);
        SwNode* cond = sw_parse_expr(ps, 0);
        if (!(lx.k == TK_OP && lx.txt[0] == ')')) sw_parse_err(ps, "expected )");
        else sw_lex_next(lx);
        SwNode* body = sw_parse_stmt(ps);
        SwNode* n = sw_anew(ps.arena);
        if (n) {
            n->k = N_WHILE;
            sw_set_child(n, 0, cond);
            sw_set_child(n, 1, body);
        }
        return n;
    }
    if (sw_tok_key(lx, "return")) {
        sw_lex_next(lx);
        SwNode* n = sw_anew(ps.arena);
        if (n) n->k = N_RET;
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) {
            if (n) sw_set_child(n, 0, sw_parse_expr(ps, 0));
        }
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        return n;
    }
    if (sw_tok_key(lx, "discard")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        SwNode* n = sw_anew(ps.arena);
        if (n) n->k = N_DISCARD;
        return n;
    }
    if (sw_tok_key(lx, "break")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        SwNode* n = sw_anew(ps.arena);
        if (n) n->k = N_BREAK;
        return n;
    }
    if (sw_tok_key(lx, "continue")) {
        sw_lex_next(lx);
        if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
        else sw_lex_next(lx);
        SwNode* n = sw_anew(ps.arena);
        if (n) n->k = N_CONT;
        return n;
    }
    SwType t = sw_type_of_name(lx.txt);
    if (t != T_VOID) {
        sw_lex_next(lx);
        if (lx.k == TK_ID) {
            char nm[20]; sw_cp(nm, sizeof(nm), lx.txt);
            sw_lex_next(lx);
            SwNode* d = sw_parse_decl_tail(ps, t, nm);
            if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
            else sw_lex_next(lx);
            SwNode* e = sw_anew(ps.arena);
            if (e) {
                e->k = N_EXPR;
                sw_set_child(e, 0, d);
            }
            return e;
        }
        sw_parse_err(ps, "bad decl");
        return 0;
    }
    SwNode* e = sw_parse_expr(ps, 0);
    SwNode* n = sw_anew(ps.arena);
    if (n) {
        n->k = N_EXPR;
        sw_set_child(n, 0, e);
    }
    if (!(lx.k == TK_OP && lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
    else sw_lex_next(lx);
    return n;
}

// ===================== shader compiler (top level) =====================
static void sw_sh_compile(SwShader& sh, const char* src) {
    sh.compiled = false;
    if (sh.arena) sw_afree(sh.arena);
    sh.arena = 0;
    sh.funcs_n = 0;
    sh.main_idx = -1;
    sh.vars_n = 0;
    sh.err[0] = 0;
    SwParser ps;
    memset(&ps, 0, sizeof(ps));
    ps.lx.p = src;
    ps.sh = &sh;
    sw_lex_next(ps.lx);
    SWDBG("compile: src=%.90s\\n", src);
    while (ps.lx.k != TK_EOF && !ps.err) {
        SWDBG("  toplevel tok k=%d txt=%.16s\\n", ps.lx.k, ps.lx.txt);
        bool uni = false, attr = false, vary = false, out = false, is_const = false;
        for (;;) {
            if (sw_tok_key(ps.lx, "uniform")) { uni = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "attribute")) { attr = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "varying")) { vary = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "in")) { if (sh.type == SW_VERTEX_SHADER) attr = true; else vary = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "out")) { out = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "const")) { is_const = true; sw_lex_next(ps.lx); continue; }
            if (sw_tok_key(ps.lx, "layout")) {
                sw_lex_next(ps.lx);
                if (ps.lx.k == TK_OP && ps.lx.txt[0] == '(') {
                    int depth = 1;
                    sw_lex_next(ps.lx);
                    while (depth > 0 && ps.lx.k != TK_EOF) {
                        if (ps.lx.k == TK_OP && ps.lx.txt[0] == '(') depth++;
                        if (ps.lx.k == TK_OP && ps.lx.txt[0] == ')') depth--;
                        sw_lex_next(ps.lx);
                    }
                }
                continue;
            }
            if (sw_tok_key(ps.lx, "highp") || sw_tok_key(ps.lx, "mediump") ||
                sw_tok_key(ps.lx, "lowp") || sw_tok_key(ps.lx, "flat") ||
                sw_tok_key(ps.lx, "noperspective") || sw_tok_key(ps.lx, "centroid")) {
                sw_lex_next(ps.lx);
                continue;
            }
            if (sw_tok_key(ps.lx, "precision")) {
                sw_lex_next(ps.lx);
                while (ps.lx.k != TK_EOF &&
                       !(ps.lx.k == TK_OP && ps.lx.txt[0] == ';')) sw_lex_next(ps.lx);
                if (ps.lx.k == TK_OP) sw_lex_next(ps.lx);
                continue;
            }
            break;
        }
        SwNode* tn = sw_parse_type_node(ps);
        if (ps.err) break;
        SwType t = tn ? (SwType)tn->op : T_VOID;
        if (ps.lx.k == TK_ID) {
            char nm[20];
            sw_cp(nm, sizeof(nm), ps.lx.txt);
            sw_lex_next(ps.lx);
            if (ps.lx.k == TK_OP && ps.lx.txt[0] == '(') {
                SwNode* f = sw_parse_function(ps, t, nm);
                if (f && sh.funcs_n < SW_MAX_FUNC) {
                    sh.funcs[sh.funcs_n++] = f;
                    if (!strcmp(nm, "main")) sh.main_idx = sh.funcs_n - 1;
                } else if (!f) sw_parse_err(ps, "function parse failed");
            } else {
                int alen = 1;
                if (ps.lx.k == TK_OP && ps.lx.txt[0] == '[') {
                    sw_lex_next(ps.lx);
                    if (ps.lx.k == TK_INT) { alen = (int)ps.lx.fv; sw_lex_next(ps.lx); }
                    if (alen < 1) alen = 1;
                    if (alen > SW_MAX_ARRAY) alen = SW_MAX_ARRAY;
                    if (!(ps.lx.k == TK_OP && ps.lx.txt[0] == ']')) sw_parse_err(ps, "expected ]");
                    else sw_lex_next(ps.lx);
                }
                sw_add_global(sh, nm, t, alen, uni, attr, vary, out, is_const);
                if (ps.lx.k == TK_OP && ps.lx.txt[0] == '=') {
                    sw_lex_next(ps.lx);
                    (void)sw_parse_expr(ps, 0);
                }
                if (!(ps.lx.k == TK_OP && ps.lx.txt[0] == ';')) sw_parse_err(ps, "expected ;");
                else sw_lex_next(ps.lx);
            }
        } else sw_parse_err(ps, "expected name");
    }
    if (ps.err) {
        if (!sh.err[0]) sw_cp(sh.err, sizeof(sh.err), "parse error");
        return;
    }
    if (sh.main_idx < 0) {
        sw_cp(sh.err, sizeof(sh.err), "no main()");
        return;
    }
    sh.compiled = true;
}

// ===================== program linking =====================
static SwShader* sw_shader_of(SwCanvas& cv, int type, unsigned sid) {
    for (int i = 0; i < SW_MAX_SH; i++)
        if (cv.sh[i].used && cv.sh[i].sid == sid && cv.sh[i].type == type) return &cv.sh[i];
    return 0;
}
static SwProg* sw_prog_of(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_PROG; i++)
        if (cv.pr[i].used && cv.pr[i].sid == sid) return &cv.pr[i];
    return 0;
}
static SwBuf_* sw_buf_of(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_BUF; i++)
        if (cv.bu[i].used && cv.bu[i].sid == sid) return &cv.bu[i];
    return 0;
}
static SwTex* sw_tex_of(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_TEX; i++)
        if (cv.tx[i].used && cv.tx[i].sid == sid) return &cv.tx[i];
    return 0;
}
static SwVao* sw_vao_of(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_VAO; i++)
        if (cv.va[i].used && cv.va[i].sid == sid) return &cv.va[i];
    return 0;
}
static int sw_prog_slot(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_PROG; i++)
        if (cv.pr[i].used && cv.pr[i].sid == sid) return i;
    return -1;
}
static int sw_buf_slot(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_BUF; i++)
        if (cv.bu[i].used && cv.bu[i].sid == sid) return i;
    return -1;
}
static int sw_tex_slot(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_TEX; i++)
        if (cv.tx[i].used && cv.tx[i].sid == sid) return i;
    return -1;
}
static int sw_vao_slot(SwCanvas& cv, unsigned sid) {
    for (int i = 0; i < SW_MAX_VAO; i++)
        if (cv.va[i].used && cv.va[i].sid == sid) return i;
    return -1;
}

static void sw_prog_link(SwCanvas& cv, SwProg& p) {
    p.linked = false;
    p.uniform_n = 0;
    p.attr_n = 0;
    p.varying_n = 0;
    SwShader* vs = p.vs >= 0 && p.vs < SW_MAX_SH && cv.sh[p.vs].used ? &cv.sh[p.vs] : 0;
    SwShader* fs = p.fs >= 0 && p.fs < SW_MAX_SH && cv.sh[p.fs].used ? &cv.sh[p.fs] : 0;
    if (!vs || !vs->compiled || !fs || !fs->compiled) {
        sw_cp(p.err, sizeof(p.err), "shaders not compiled");
        return;
    }
    // uniforms: union of VS + FS names
    for (int i = 0; i < vs->vars_n; i++) {
        SwVar& v = vs->vars[i];
        if (!v.is_uniform) continue;
        if (p.uniform_n >= SW_MAX_UNIFORMS) break;
        sw_cp(p.uniform_name[p.uniform_n], 20, v.name);
        p.uniform_vs[p.uniform_n] = i;
        p.uniform_fs[p.uniform_n] = -1;
        for (int j = 0; j < fs->vars_n; j++)
            if (fs->vars[j].is_uniform && !strcmp(fs->vars[j].name, v.name)) { p.uniform_fs[p.uniform_n] = j; break; }
        p.uniform_n++;
    }
    for (int i = 0; i < fs->vars_n; i++) {
        SwVar& v = fs->vars[i];
        if (!v.is_uniform) continue;
        bool dup = false;
        for (int j = 0; j < p.uniform_n; j++)
            if (!strcmp(p.uniform_name[j], v.name)) { dup = true; break; }
        if (dup) continue;
        if (p.uniform_n >= SW_MAX_UNIFORMS) break;
        sw_cp(p.uniform_name[p.uniform_n], 20, v.name);
        p.uniform_vs[p.uniform_n] = -1;
        for (int j = 0; j < vs->vars_n; j++)
            if (vs->vars[j].is_uniform && !strcmp(vs->vars[j].name, v.name)) { p.uniform_vs[p.uniform_n] = j; break; }
        p.uniform_fs[p.uniform_n] = i;
        p.uniform_n++;
    }
    // attributes: VS in/attribute vars, global slots assigned in var order
    int slot = 0;
    for (int i = 0; i < vs->vars_n && slot < SW_MAX_ATTRIBS; i++) {
        SwVar& v = vs->vars[i];
        if (!v.is_attr) continue;
        if (p.attr_n >= SW_MAX_UNIFORMS) break;
        sw_cp(p.attr_name[p.attr_n], 20, v.name);
        p.attr_var[p.attr_n] = i;
        p.attr_slot[p.attr_n] = slot++;
        p.attr_n++;
    }
    // varyings: FS in vars matched by name to VS out/varying vars
    for (int i = 0; i < fs->vars_n; i++) {
        SwVar& v = fs->vars[i];
        if (!v.is_varying || v.is_out) continue;
        if (p.varying_n >= SW_MAX_FRAGV) break;
        int vsi = -1;
        for (int j = 0; j < vs->vars_n; j++) {
            SwVar& w = vs->vars[j];
            if ((w.is_varying || w.is_out) && !strcmp(w.name, v.name)) { vsi = j; break; }
        }
        if (vsi < 0) {
            sw_cp(p.err, sizeof(p.err), "unmatched varying");
            return;
        }
        sw_cp(p.varying_fs_name[p.varying_n], 20, v.name);
        p.varying_fs_var[p.varying_n] = i;
        p.varying_vs_var[p.varying_n] = vsi;
        p.varying_n++;
    }
    p.linked = true;
    p.err[0] = 0;
}

// ===================== evaluator =====================
struct SwFrame {
    SwShader* sh;
    SwCanvas* cv;
    SwVal locals[SW_MAX_LOCAL];
    bool used[SW_MAX_LOCAL];
    char name[SW_MAX_LOCAL][20];
    int n;
    bool brk, cont, ret, discard;
    SwVal retval;
    SwFrame* up;
};
static SwVal sw_eval(SwNode* n, SwFrame& f);
static void sw_run_stmt(SwNode* stmt, SwFrame& f);

static SwVal sw_eval_ident(const char* name, SwFrame& f) {
    SwCanvas& cv = *f.cv;
    if (!strcmp(name, "gl_Position")) {
        SwVal v = sw_val(T_V4);
        v.f[0] = cv.gl_pos[0]; v.f[1] = cv.gl_pos[1]; v.f[2] = cv.gl_pos[2]; v.f[3] = cv.gl_pos[3];
        return v;
    }
    if (!strcmp(name, "gl_PointSize")) { SwVal v = sw_val(T_F); v.f[0] = cv.gl_psize; return v; }
    if (!strcmp(name, "gl_FragCoord")) return cv.gl_fragcoord;
    if (!strcmp(name, "gl_FragColor")) {
        SwVal v = sw_val(T_V4);
        v.f[0] = cv.fragcolor[0]; v.f[1] = cv.fragcolor[1]; v.f[2] = cv.fragcolor[2]; v.f[3] = cv.fragcolor[3];
        return v;
    }
    if (!strcmp(name, "gl_FragDepth")) { SwVal v = sw_val(T_F); v.f[0] = cv.fragdepth; return v; }
    if (!strcmp(name, "gl_PointCoord")) { SwVal v = sw_val(T_V2); v.f[0] = 0.5f; v.f[1] = 0.5f; return v; }
    if (f.sh) {
        for (int i = 0; i < f.sh->vars_n; i++)
            if (!strcmp(f.sh->vars[i].name, name)) return f.sh->vars[i].val;
    }
    // locals: walk frames
    for (SwFrame* fr = &f; fr; fr = fr->up) {
        for (int i = 0; i < fr->n; i++)
            if (fr->used[i] && !strcmp(fr->name[i], name)) return fr->locals[i];
    }
    return sw_val(T_F);   // unresolved -> 0.0
}

// resolve the storage of an lvalue expression; fills base/comps/compn
static bool sw_lval(SwNode* n, SwFrame& f, SwVal** base, float** comps, int* compn) {
    if (!n) return false;
    if (n->k == N_IDENT) {
        SwVal* v = 0;
        if (f.sh) {
            for (int i = 0; i < f.sh->vars_n; i++)
                if (!strcmp(f.sh->vars[i].name, n->name)) { v = &f.sh->vars[i].val; break; }
        }
        if (!v) {
            for (SwFrame* fr = &f; fr; fr = fr->up)
                for (int i = 0; i < fr->n; i++)
                    if (fr->used[i] && !strcmp(fr->name[i], n->name)) { v = &fr->locals[i]; break; }
        }
        // built-in outputs
        if (!v) {
            SwCanvas& cv = *f.cv;
            if (!strcmp(n->name, "gl_Position")) { *base = 0; *comps = cv.gl_pos; *compn = 4; return true; }
            if (!strcmp(n->name, "gl_Position")) { *base = 0; *comps = cv.gl_pos; *compn = 4; return true; }
            if (!strcmp(n->name, "gl_FragColor")) { *base = 0; *comps = cv.fragcolor; *compn = 4; return true; }
            if (!strcmp(n->name, "gl_FragDepth")) { *base = 0; *comps = &cv.fragdepth; *compn = 1; return true; }
            return false;
        }
        *base = v;
        *comps = v->f;
        *compn = sw_tsize(v->t) * (v->array_len > 1 ? v->array_len : 1);
        return true;
    }
    if (n->k == N_INDEX) {
        SwNode* baseN = (SwNode*)n->a;
        SwNode* idxN = (SwNode*)n->b;
        SwVal* b;
        float* cp;
        int cn;
        if (!sw_lval(baseN, f, &b, &cp, &cn)) return false;
        float idx = sw_eval(idxN, f).f[0];
        int i = (int)idx;
        if (i < 0) i = 0;
        int sz = b ? sw_tsize(b->t) : 1;
        if (i * sz >= 16) return false;
        *base = b;
        *comps = cp + i * sz;
        *compn = sz;
        return true;
    }
    if (n->k == N_SWIZ) {
        SwNode* baseN = (SwNode*)n->a;
        SwVal* b;
        float* cp;
        int cn;
        if (!sw_lval(baseN, f, &b, &cp, &cn)) return false;
        if (n->swz_n == 0) return false;
        *base = 0;
        static float scratch[4];
        for (int i = 0; i < n->swz_n; i++) scratch[i] = cp[n->swz[i]];
        *comps = scratch;
        *compn = n->swz_n;
        return true;
    }
    return false;
}

// store into lvalue
static void sw_store(SwNode* lval, const SwVal& v, SwFrame& f) {
    if (!lval) return;
    if (lval->k == N_IDENT) {
        // find the storage and write the whole value (same type)
        for (int i = 0; i < f.sh->vars_n; i++)
            if (!strcmp(f.sh->vars[i].name, lval->name)) { f.sh->vars[i].val = v; return; }
        for (SwFrame* fr = &f; fr; fr = fr->up)
            for (int i = 0; i < fr->n; i++)
                if (fr->used[i] && !strcmp(fr->name[i], lval->name)) { fr->locals[i] = v; return; }
        // built-in outputs
        SwCanvas& cv = *f.cv;
        int n = sw_tsize(v.t); if (n > 4) n = 4;
        if (!strcmp(lval->name, "gl_Position")) { for (int i = 0; i < n; i++) cv.gl_pos[i] = v.f[i]; SWDBG("  store gl_Position v=(%.2f,%.2f,%.2f,%.2f)\n", v.f[0], v.f[1], v.f[2], v.f[3]); return; }
        if (!strcmp(lval->name, "gl_PointSize")) { cv.gl_psize = v.f[0]; return; }
        if (!strcmp(lval->name, "gl_FragColor")) { for (int i = 0; i < n; i++) cv.fragcolor[i] = v.f[i]; return; }
        if (!strcmp(lval->name, "gl_FragDepth")) { cv.fragdepth = v.f[0]; return; }
        return;
    }
    if (lval->k == N_INDEX || lval->k == N_SWIZ) {
        SwVal* b;
        float* cp;
        int cn;
        if (sw_lval(lval, f, &b, &cp, &cn)) {
            int sz = sw_tsize(v.t);
            if (sz > cn) sz = cn;
            for (int i = 0; i < sz; i++) cp[i] = v.f[i];
        }
        return;
    }
}

static bool sw_boolv(const SwVal& v) {
    for (int i = 0; i < sw_tsize(v.t); i++)
        if (v.f[i] != 0.0f) return true;
    return false;
}
static SwVal sw_scalar(float x) { SwVal v = sw_val(T_F); v.f[0] = x; return v; }
static SwVal sw_intv(int x) { SwVal v = sw_val(T_I); v.f[0] = (float)x; return v; }

static SwVal sw_vecn(int n, float a, float b, float c, float d) {
    SwType t = n == 2 ? T_V2 : (n == 3 ? T_V3 : T_V4);
    SwVal v = sw_val(t);
    v.f[0] = a; v.f[1] = b; v.f[2] = c; v.f[3] = d;
    return v;
}


// binary op
static SwVal sw_bin(int op, const SwVal& A, const SwVal& B) {
    SwType at = (SwType)A.t, bt = (SwType)B.t;
    bool aM = at >= T_M2 && at <= T_M4;
    bool bM = bt >= T_M2 && bt <= T_M4;
    bool aV = at >= T_V2 && at <= T_V4;
    bool bV = bt >= T_V2 && bt <= T_V4;
    // matrix * vector
    if (aM && bV && op == OP_MUL) {
        int N = at == T_M2 ? 2 : (at == T_M3 ? 3 : 4);
        int M = sw_tsize(bt);
        SwVal r = sw_val(N == 4 ? T_V4 : (N == 3 ? T_V3 : T_V2));
        for (int i = 0; i < N; i++) {
            float s = 0.0f;
            for (int j = 0; j < M; j++) s += A.f[j * N + i] * B.f[j];
            r.f[i] = s;
        }
        return r;
    }
    if (aV && bM && op == OP_MUL) {
        int N = sw_tsize(at);
        int M = bt == T_M2 ? 2 : (bt == T_M3 ? 3 : 4);
        SwVal r = sw_val(M == 4 ? T_V4 : (M == 3 ? T_V3 : T_V2));
        for (int j = 0; j < M; j++) {
            float s = 0.0f;
            for (int i = 0; i < N; i++) s += A.f[i] * B.f[j * N + i];
            r.f[j] = s;
        }
        return r;
    }
    // matrix * matrix
    if (aM && bM && op == OP_MUL) {
        int N = at == T_M2 ? 2 : (at == T_M3 ? 3 : 4);   // rows of A, cols of B
        SwVal r = sw_val((SwType)(at));
        for (int col = 0; col < N; col++)
            for (int row = 0; row < N; row++) {
                float s = 0.0f;
                for (int k = 0; k < N; k++) s += A.f[k * N + row] * B.f[col * N + k];
                r.f[col * N + row] = s;
            }
        return r;
    }
    // matrix/scalar ops
    if (aM && bt == T_F) {
        SwVal r = A;
        for (int i = 0; i < sw_tsize(at); i++) {
            float x = A.f[i], y = B.f[0];
            r.f[i] = op == OP_ADD ? x + y : op == OP_SUB ? x - y :
                     op == OP_MUL ? x * y : op == OP_DIV ? x / y : 0.0f;
        }
        return r;
    }
    if (bM && at == T_F) {
        SwVal r = B;
        for (int i = 0; i < sw_tsize(bt); i++) {
            float x = A.f[0], y = B.f[i];
            r.f[i] = op == OP_ADD ? x + y : op == OP_SUB ? x - y :
                     op == OP_MUL ? x * y : op == OP_DIV ? x / y : 0.0f;
        }
        return r;
    }
    // comparisons (scalar or vector equality)
    if (op == OP_EQ || op == OP_NE) {
        int n = sw_maxf((float)sw_tsize(at), (float)sw_tsize(bt));
        bool eq = true;
        for (int i = 0; i < n; i++) {
            float x = sw_tsize(at) > 1 ? A.f[i] : A.f[0];
            float y = sw_tsize(bt) > 1 ? B.f[i] : B.f[0];
            if (x != y) { eq = false; break; }
        }
        return sw_scalar(op == OP_EQ ? (eq ? 1.0f : 0.0f) : (eq ? 0.0f : 1.0f));
    }
    if (op >= OP_LT && op <= OP_GE) {
        float x = A.f[0], y = B.f[0];
        bool r = op == OP_LT ? x < y : op == OP_LE ? x <= y : op == OP_GT ? x > y : x >= y;
        return sw_scalar(r ? 1.0f : 0.0f);
    }
    if (op == OP_AND) return sw_scalar((sw_boolv(A) && sw_boolv(B)) ? 1.0f : 0.0f);
    if (op == OP_OR) return sw_scalar((sw_boolv(A) || sw_boolv(B)) ? 1.0f : 0.0f);
    if (op == OP_BAND || op == OP_BOR || op == OP_BXOR) {
        int x = (int)A.f[0], y = (int)B.f[0];
        return sw_intv(op == OP_BAND ? (x & y) : op == OP_BOR ? (x | y) : (x ^ y));
    }
    // arithmetic, component-wise with scalar broadcast
    int n = sw_maxf((float)sw_tsize(at), (float)sw_tsize(bt));
    if (n > 4) n = 4;
    SwVal r = sw_val(n <= 2 ? (n == 2 ? T_V2 : T_F) : (n == 3 ? T_V3 : T_V4));
    for (int i = 0; i < n; i++) {
        float x = sw_tsize(at) > 1 ? A.f[i] : A.f[0];
        float y = sw_tsize(bt) > 1 ? B.f[i] : B.f[0];
        r.f[i] = op == OP_ADD ? x + y : op == OP_SUB ? x - y :
                 op == OP_MUL ? x * y : op == OP_DIV ? x / y :
                 op == OP_MOD ? sw_modf(x, y) : 0.0f;
    }
    return r;
}

static SwVal sw_un(int op, const SwVal& A) {
    if (op == OP_NEG) {
        SwVal r = A;
        for (int i = 0; i < sw_tsize(A.t); i++) r.f[i] = -A.f[i];
        return r;
    }
    if (op == OP_NOT) return sw_scalar(sw_boolv(A) ? 0.0f : 1.0f);
    if (op == OP_BNOT) return sw_intv(~(int)A.f[0]);
    return A;
}

// builtin math
static SwVal sw_math1(const SwVal& a, float (*fn)(float)) {
    if (a.t >= T_V2 && a.t <= T_V4) {
        SwVal r = a;
        for (int i = 0; i < sw_tsize(a.t); i++) r.f[i] = fn(a.f[i]);
        return r;
    }
    return sw_scalar(fn(a.f[0]));
}
static SwVal sw_math2(const SwVal& a, const SwVal& b, float (*fn)(float, float)) {
    if (a.t >= T_V2 && a.t <= T_V4) {
        SwVal r = a;
        for (int i = 0; i < sw_tsize(a.t); i++) {
            float y = b.t >= T_V2 && b.t <= T_V4 ? b.f[i] : b.f[0];
            r.f[i] = fn(a.f[i], y);
        }
        return r;
    }
    return sw_scalar(fn(a.f[0], b.f[0]));
}

static SwVal sw_constructor(int type, SwVal* args, int argc) {
    int n = sw_tsize((SwType)type);
    SwVal r = sw_val((SwType)type);
    // single scalar arg to vec/mat
    if (type >= T_V2 && type <= T_V4 && argc == 1 && args[0].t == T_F) {
        for (int i = 0; i < n; i++) r.f[i] = args[0].f[0];
        return r;
    }
    if (type == T_M2 || type == T_M3 || type == T_M4) {
        if (argc == 1) {
            if (args[0].t == T_F) {
                for (int i = 0; i < n; i++) r.f[i] = (i % (n + 1) == 0) ? args[0].f[0] : 0.0f;
                return r;
            }
            if (args[0].t >= T_M2 && args[0].t <= T_M4) {   // mat from mat
                int sn = sw_tsize(args[0].t);
                for (int i = 0; i < n && i < sn; i++) r.f[i] = args[0].f[i];
                return r;
            }
        }
        // columns from vector args
        int col = 0, comp = 0;
        for (int a = 0; a < argc && col < n; a++) {
            for (int i = 0; i < sw_tsize(args[a].t) && col < n; i++) {
                r.f[col * n + comp] = args[a].f[i];
                comp++;
                if (comp >= n) { comp = 0; col++; }
            }
        }
        return r;
    }
    // vec from components (or single vec)
    if (type >= T_V2 && type <= T_V4) {
        int k = 0;
        for (int a = 0; a < argc && k < n; a++) {
            for (int i = 0; i < sw_tsize(args[a].t) && k < n; i++) r.f[k++] = args[a].f[i];
        }
        return r;
    }
    if (type == T_F) return sw_scalar(argc > 0 ? args[0].f[0] : 0.0f);
    if (type == T_I) return sw_intv(argc > 0 ? (int)args[0].f[0] : 0);
    if (type == T_B) return sw_scalar(argc > 0 && sw_boolv(args[0]) ? 1.0f : 0.0f);
    return r;
}

static float sw_tex_wrap(float v, int mode, int size) {
    if (mode == SW_REPEAT) {
        v = v - sw_floorf(v);
    } else if (mode == SW_MIRRORED_REPEAT) {
        float t = sw_modf(v, 2.0f);
        v = t > 1.0f ? 2.0f - t : t;
        if (v > 1.0f) v = 1.0f;
        if (v < 0.0f) v = 0.0f;
    } else {
        v = sw_clampf(v, 0.0f, 1.0f);
    }
    float p = v * (float)size;
    if (p >= (float)size) p = (float)(size - 1);
    if (p < 0.0f) p = 0.0f;
    return p;
}
static SwVal sw_tex_sample(SwCanvas& cv, int unit, float u, float v) {
    SwVal r = sw_val(T_V4);
    if (unit < 0 || unit >= 8) return r;
    int ti = cv.unit_tex[unit];
    if (ti < 0) return r;
    SwTex& tx = cv.tx[ti];
    if (!tx.used || !tx.rgba || tx.w <= 0 || tx.h <= 0) return r;
    float uf = u, vf = v;
    if (cv.unpack_flip_y) vf = 1.0f - vf;
    if (tx.mag_filter == SW_NEAREST || tx.min_filter == SW_NEAREST) {
        int x = (int)sw_tex_wrap(uf, tx.wrap_s, tx.w);
        int y = (int)sw_tex_wrap(vf, tx.wrap_t, tx.h);
        const uint8_t* p = tx.rgba + (y * tx.w + x) * 4;
        r.f[0] = p[0] / 255.0f; r.f[1] = p[1] / 255.0f; r.f[2] = p[2] / 255.0f; r.f[3] = p[3] / 255.0f;
        return r;
    }
    // bilinear
    auto smp = [&](float uu, float vv) -> SwVal {
        int x = (int)sw_tex_wrap(uu, tx.wrap_s, tx.w);
        int y = (int)sw_tex_wrap(vv, tx.wrap_t, tx.h);
        const uint8_t* p = tx.rgba + (y * tx.w + x) * 4;
        SwVal s = sw_val(T_V4);
        s.f[0] = p[0] / 255.0f; s.f[1] = p[1] / 255.0f; s.f[2] = p[2] / 255.0f; s.f[3] = p[3] / 255.0f;
        return s;
    };
    float fx = uf * (float)tx.w - 0.5f;
    float fy = vf * (float)tx.h - 0.5f;
    float ix = sw_floorf(fx), iy = sw_floorf(fy);
    float txw = fx - ix, tyw = fy - iy;
    // sample using wrap on fractional coordinates
    auto wrapu = [&](float c) { return sw_tex_wrap((c + 0.5f) / (float)tx.w, tx.wrap_s, tx.w); };
    auto wrapv = [&](float c) { return sw_tex_wrap((c + 0.5f) / (float)tx.h, tx.wrap_t, tx.h); };
    SwVal c00 = smp(wrapu(ix), wrapv(iy));
    SwVal c10 = smp(wrapu(ix + 1.0f), wrapv(iy));
    SwVal c01 = smp(wrapu(ix), wrapv(iy + 1.0f));
    SwVal c11 = smp(wrapu(ix + 1.0f), wrapv(iy + 1.0f));
    for (int i = 0; i < 4; i++) {
        float top = c00.f[i] * (1.0f - txw) + c10.f[i] * txw;
        float bot = c01.f[i] * (1.0f - txw) + c11.f[i] * txw;
        r.f[i] = top * (1.0f - tyw) + bot * tyw;
    }
    return r;
}

static SwVal sw_call_builtin(const char* name, SwVal* args, int argc, SwFrame& f) {
    SwCanvas& cv = *f.cv;

    if (!strcmp(name, "texture") || !strcmp(name, "textureLod")) {
        if (argc >= 2) {
            int unit = (int)args[0].f[0];
            float u = args[1].f[0], v = args[1].f[1];
            SWDBG("  tex() x=%.1f y=%.1f unit=%d u=%.4f v=%.4f\n", cv.gl_fragcoord.f[0], cv.gl_fragcoord.f[1], unit, u, v);
            return sw_tex_sample(cv, unit, u, v);
        }
        return sw_val(T_V4);
    }
    if (!strcmp(name, "textureProj")) {
        if (argc >= 2) {
            int unit = (int)args[0].f[0];
            float w = args[1].f[2] != 0.0f ? args[1].f[2] : 1.0f;
            return sw_tex_sample(cv, unit, args[1].f[0] / w, args[1].f[1] / w);
        }
        return sw_val(T_V4);
    }
    if (!strcmp(name, "dFdx") || !strcmp(name, "dFdy") || !strcmp(name, "fwidth") ||
        !strcmp(name, "dFdxFine") || !strcmp(name, "dFdyFine") || !strcmp(name, "fwidthFine") ||
        !strcmp(name, "dFdxCoarse") || !strcmp(name, "dFdyCoarse") || !strcmp(name, "fwidthCoarse"))
        return argc > 0 ? args[0] : sw_val(T_F);

    // component-wise math
    struct { const char* n; float (*fn)(float); } un[] = {
        { "sin", sw_sinf }, { "cos", sw_cosf }, { "tan", sw_tanf },
        { "asin", sw_asin_safe }, { "acos", sw_acos_safe }, { "atan", sw_atanf },
        { "sqrt", sw_sqrtf }, { "inversesqrt", sw_rsqrtf }, { "abs", sw_absf },
        { "sign", sw_signf }, { "floor", sw_floorf }, { "ceil", sw_ceilf },
        { "fract", sw_fractf }, { "exp", sw_expf }, { "exp2", sw_exp2f },
        { "log", sw_logf }, { "log2", sw_log2f }, { "trunc", sw_floorf },
    };
    for (unsigned i = 0; i < sizeof(un) / sizeof(un[0]); i++)
        if (!strcmp(name, un[i].n) && argc == 1) return sw_math1(args[0], un[i].fn);

    if (!strcmp(name, "round") && argc == 1) {
        return sw_math1(args[0], [](float x) { return sw_floorf(x + 0.5f); });
    }
    if (!strcmp(name, "radians") && argc == 1) {
        return sw_math1(args[0], [](float x) { return x * SW_PI / 180.0f; });
    }
    if (!strcmp(name, "degrees") && argc == 1) {
        return sw_math1(args[0], [](float x) { return x * 180.0f / SW_PI; });
    }
    if (!strcmp(name, "atan") && argc == 2) return sw_math2(args[0], args[1], sw_atan2f);
    if (!strcmp(name, "pow") && argc == 2) return sw_math2(args[0], args[1], sw_powf);
    if (!strcmp(name, "mod") && argc == 2) return sw_math2(args[0], args[1], sw_modf);
    if (!strcmp(name, "min") && argc == 2) return sw_math2(args[0], args[1], sw_minf);
    if (!strcmp(name, "max") && argc == 2) return sw_math2(args[0], args[1], sw_maxf);
    if (!strcmp(name, "clamp") && argc == 3)
        return sw_math2(sw_math2(args[0], args[1], sw_maxf), args[2], sw_minf);
    if (!strcmp(name, "mix") && argc == 3)
        return sw_mix(args[0], args[1], args[2]);
    if (!strcmp(name, "step") && argc == 2)
        return sw_step(args[0], args[1]);
    if (!strcmp(name, "smoothstep") && argc == 3)
        return sw_smooth(args[0], args[1], args[2]);
    // vector built-ins
    auto vlen = [](const SwVal& a) {
        float s = 0.0f;
        for (int i = 0; i < sw_tsize(a.t); i++) s += a.f[i] * a.f[i];
        return sw_sqrtf(s);
    };
    if (!strcmp(name, "length") && argc == 1) return sw_scalar(vlen(args[0]));
    if (!strcmp(name, "distance") && argc == 2) {
        SwVal d = sw_bin(OP_SUB, args[0], args[1]);
        return sw_scalar(vlen(d));
    }
    if (!strcmp(name, "dot") && argc == 2) {
        float s = 0.0f;
        for (int i = 0; i < sw_tsize(args[0].t); i++) s += args[0].f[i] * args[1].f[i];
        return sw_scalar(s);
    }
    if (!strcmp(name, "cross") && argc == 2) {
        SwVal r = sw_val(T_V3);
        r.f[0] = args[0].f[1] * args[1].f[2] - args[0].f[2] * args[1].f[1];
        r.f[1] = args[0].f[2] * args[1].f[0] - args[0].f[0] * args[1].f[2];
        r.f[2] = args[0].f[0] * args[1].f[1] - args[0].f[1] * args[1].f[0];
        return r;
    }
    if (!strcmp(name, "normalize") && argc == 1) {
        SwVal r = args[0];
        float l = vlen(args[0]);
        if (l > 0.0f)
            for (int i = 0; i < sw_tsize(args[0].t); i++) r.f[i] /= l;
        return r;
    }
    if (!strcmp(name, "reflect") && argc == 2) {
        SwVal n = args[1];
        float d = 0.0f;
        for (int i = 0; i < sw_tsize(args[0].t); i++) d += args[0].f[i] * n.f[i];
        SwVal r = args[0];
        for (int i = 0; i < sw_tsize(args[0].t); i++) r.f[i] -= 2.0f * d * n.f[i];
        return r;
    }
    if (!strcmp(name, "faceforward") && argc == 3) {
        float d = 0.0f;
        for (int i = 0; i < sw_tsize(args[1].t); i++) d += args[1].f[i] * args[2].f[i];
        return d < 0.0f ? args[0] : sw_un(OP_NEG, args[0]);
    }
    if (!strcmp(name, "refract") && argc == 3) {
        SwVal r = args[0];
        float eta = args[2].f[0];
        float d = 0.0f;
        for (int i = 0; i < sw_tsize(args[0].t); i++) d += args[0].f[i] * args[1].f[i];
        float k = 1.0f - eta * eta * (1.0f - d * d);
        if (k < 0.0f) {
            for (int i = 0; i < sw_tsize(args[0].t); i++) r.f[i] = 0.0f;
        } else {
            float c = eta * d + sw_sqrtf(k);
            for (int i = 0; i < sw_tsize(args[0].t); i++) r.f[i] = eta * args[0].f[i] - c * args[1].f[i];
        }
        return r;
    }
    if (!strcmp(name, "transpose") && argc == 1) {
        int n = sw_tsize(args[0].t);
        SwVal r = args[0];
        for (int col = 0; col < n; col++)
            for (int row = 0; row < n; row++) r.f[col * n + row] = args[0].f[row * n + col];
        return r;
    }
    if (!strcmp(name, "inverse") || !strcmp(name, "inversemat")) {
        int n = sw_tsize(args[0].t);
        SwVal r = sw_val((SwType)args[0].t);
        for (int i = 0; i < n; i++) r.f[i * n + i] = 1.0f;
        return r;   // identity placeholder (documented limitation)
    }
    if (!strcmp(name, "matrixCompMult") && argc == 2) {
        SwVal r = args[0];
        for (int i = 0; i < sw_tsize(args[0].t); i++) r.f[i] *= args[1].f[i];
        return r;
    }
    if (!strcmp(name, "lessThan") || !strcmp(name, "greaterThan") ||
        !strcmp(name, "lessThanEqual") || !strcmp(name, "greaterThanEqual") ||
        !strcmp(name, "equal") || !strcmp(name, "notEqual") ||
        !strcmp(name, "not")) {
        int n = sw_tsize(args[0].t);
        SwVal r = sw_val(n == 2 ? T_V2 : (n == 3 ? T_V3 : T_V4));
        for (int i = 0; i < n; i++) {
            bool c;
            if (!strcmp(name, "not")) c = args[0].f[i] == 0.0f;
            else if (!strcmp(name, "equal")) c = args[0].f[i] == args[1].f[i];
            else if (!strcmp(name, "notEqual")) c = args[0].f[i] != args[1].f[i];
            else if (!strcmp(name, "lessThan")) c = args[0].f[i] < args[1].f[i];
            else if (!strcmp(name, "greaterThan")) c = args[0].f[i] > args[1].f[i];
            else if (!strcmp(name, "lessThanEqual")) c = args[0].f[i] <= args[1].f[i];
            else c = args[0].f[i] >= args[1].f[i];
            r.f[i] = c ? 1.0f : 0.0f;
        }
        return r;
    }
    // any other built-in -> 0 / identity
    return sw_val(T_F);
}

static SwVal sw_eval(SwNode* n, SwFrame& f) {
    if (!n) return sw_val(T_F);
    switch (n->k) {
        case N_INT:
        case N_FLOAT: {
            SwVal v = sw_val(n->k == N_INT ? T_I : T_F);
            v.f[0] = n->fv;
            return v;
        }
        case N_IDENT:
            return sw_eval_ident(n->name, f);
        case N_BIN: {
            SwVal _ba = sw_eval((SwNode*)n->a, f);
            SwVal _bb = sw_eval((SwNode*)n->b, f);
            SWDBG("    bin op=%d a=%.3f b=%.3f -> %.3f\\n", n->op, _ba.f[0], _bb.f[0], sw_bin(n->op, _ba, _bb).f[0]);
            return sw_bin(n->op, _ba, _bb);
        }
        case N_UN:
            return sw_un(n->op, sw_eval((SwNode*)n->a, f));
        case N_TERN:
            return sw_boolv(sw_eval((SwNode*)n->a, f)) ? sw_eval((SwNode*)n->b, f)
                                                       : sw_eval((SwNode*)n->c, f);
        case N_INDEX: {
            SwVal base = sw_eval((SwNode*)n->a, f);
            int i = (int)sw_eval((SwNode*)n->b, f).f[0];
            int sz = sw_tsize(base.t);
            int ncomp = sz * (base.array_len > 1 ? base.array_len : 1);
            if (i < 0) i = 0;
            if (i * sz >= ncomp || i * sz >= 16) return sw_val(base.t);
            if (base.t == T_F || base.t == T_I || base.t == T_B) {
                SwVal v = sw_val(base.t);
                v.f[0] = base.f[i];
                return v;
            }
            SwVal v = sw_val(base.t);
            v.array_len = 1;
            for (int k = 0; k < sz; k++) v.f[k] = base.f[i * sz + k];
            return v;
        }
        case N_SWIZ: {
            SwVal base = sw_eval((SwNode*)n->a, f);
            SwVal v = sw_val(n->swz_n == 2 ? T_V2 : (n->swz_n == 3 ? T_V3 : T_V4));
            int sz = sw_tsize(base.t);
            int ncomp = sz * (base.array_len > 1 ? base.array_len : 1);
            if (ncomp > 16) ncomp = 16;
            for (int i = 0; i < n->swz_n; i++) {
                int c = n->swz[i];
                if (c < ncomp) v.f[i] = base.f[c];
            }
            return v;
        }
        case N_CALL: {
            if (n->op == OP_CALL) {
                SwVal args[4];
                int argc = 0;
                SwNode* as[4] = { (SwNode*)n->a, (SwNode*)n->b, (SwNode*)n->c, (SwNode*)n->d };
                for (int i = 0; i < 4; i++) {
                    if (as[i]) { args[argc] = sw_eval(as[i], f); argc++; }
                }
                // user functions?
                if (f.sh) {
                    for (int fi = 0; fi < f.sh->funcs_n; fi++) {
                        SwNode* fn = f.sh->funcs[fi];
                        if (fn && !strcmp(fn->name, n->name)) {
                            SwFrame nf;
                            memset(&nf, 0, sizeof(nf));
                            nf.sh = f.sh;
                            nf.cv = f.cv;
                            nf.up = &f;
                            // params into locals 0..argc-1 (unwrap N_BLOCK wrappers)
                            SwNode* pn = (SwNode*)fn->a;
                            int pi = 0;
                            while (pn && pi < argc) {
                                SwNode* prm = (SwNode*)pn->a;
                                nf.used[pi] = true;
                                sw_cp(nf.name[pi], 20, prm ? prm->name : pn->name);
                                nf.locals[pi] = args[pi];
                                pi++;
                                pn = (SwNode*)pn->b;
                            }
                            nf.n = pi;
                            SWDBG("    call %s argc=%d arg0=%.2f nf.n=%d\n", fn->name, argc, nf.locals[0].f[0], nf.n);
                            sw_run_stmt((SwNode*)fn->b, nf);
                            SWDBG("    call %s ret=%d retval=(%.2f,%.2f,%.2f,%.2f)\n", fn->name, nf.ret, nf.retval.f[0], nf.retval.f[1], nf.retval.f[2], nf.retval.f[3]);
                            if (nf.ret) return nf.retval;
                            return sw_val((SwType)fn->op);
                        }
                    }
                }
                return sw_call_builtin(n->name, args, argc, f);
            }
            // constructor call
            SwVal args[4];
            int argc = 0;
            SwNode* as[4] = { (SwNode*)n->a, (SwNode*)n->b, (SwNode*)n->c, (SwNode*)n->d };
            for (int i = 0; i < 4; i++) {
                if (as[i]) { args[argc] = sw_eval(as[i], f); argc++; }
            }
            SWDBG("  ctor op=%d argc=%d a0.t=%d a0=(%.2f,%.2f,%.2f,%.2f) a1=(%.2f,%.2f) a2=(%.2f,%.2f)\n", n->op, argc, args[0].t, args[0].f[0], args[0].f[1], args[0].f[2], args[0].f[3], args[1].f[0], args[1].f[1], args[2].f[0], args[2].f[1]);
            return sw_constructor(n->op, args, argc);
        }
        case N_ASSIGN: {
            SwVal v = sw_eval((SwNode*)n->b, f);
            if (n->op != OP_ASSIGN) {
                SwVal cur = sw_eval((SwNode*)n->a, f);
                int op = n->op == OP_AADD ? OP_ADD : n->op == OP_ASUB ? OP_SUB :
                         n->op == OP_AMUL ? OP_MUL : n->op == OP_ADIV ? OP_DIV : OP_MOD;
                v = sw_bin(op, cur, v);
            }
            sw_store((SwNode*)n->a, v, f);
            return v;
        }
        default:
            return sw_val(T_F);
    }
}

static void sw_run_stmt(SwNode* stmt, SwFrame& f) {
    if (!stmt) return;
    switch (stmt->k) {
        case N_BLOCK:
            while (stmt) {
                sw_run_stmt((SwNode*)stmt->a, f);
                if (f.brk || f.cont || f.ret || f.discard) return;
                stmt = (SwNode*)stmt->b;
            }
            return;
        case N_EXPR: {
            SwNode* e = (SwNode*)stmt->a;
            if (e && e->k == N_DECL) { sw_run_stmt(e, f); return; }
            sw_eval(e, f);
            return;
        }
        case N_DECL: {
            int slot = f.n;
            if (slot < SW_MAX_LOCAL) {
                f.used[slot] = true;
                sw_cp(f.name[slot], 20, stmt->name);
                SwType t = (SwType)stmt->op;
                f.locals[slot] = sw_val(t);
                f.locals[slot].array_len = stmt->d > 1 ? (int)stmt->d : 1;
                f.n = slot + 1;
                if (stmt->a) f.locals[slot] = sw_eval((SwNode*)stmt->a, f);
            }
            return;
        }
        case N_IF:
            if (sw_boolv(sw_eval((SwNode*)stmt->a, f)))
                sw_run_stmt((SwNode*)stmt->b, f);
            else if (stmt->c)
                sw_run_stmt((SwNode*)stmt->c, f);
            return;
        case N_FOR: {
            SwNode* init = (SwNode*)stmt->a;
            SwNode* cond = (SwNode*)stmt->b;
            SwNode* upd = (SwNode*)stmt->c;
            SwNode* body = (SwNode*)stmt->d;
            if (init) sw_run_stmt(init, f);
            int guard = 4096;
            while (guard-- > 0 && !f.ret && !f.discard) {
                if (cond && !sw_boolv(sw_eval(cond, f))) break;
                SWDBG("    for iter guard=%d n=%d [%s]=%.2f [%s]=%.2f [%s]=%.2f\n", guard, f.n, f.name[0], f.locals[0].f[0], f.name[1], f.locals[1].f[0], f.name[2], f.locals[2].f[0]);
                sw_run_stmt(body, f);
                if (f.brk) { f.brk = false; break; }
                if (f.cont) { f.cont = false; }
                if (f.ret || f.discard) break;
                if (upd) sw_eval(upd, f);
            }
            return;
        }
        case N_WHILE: {
            SwNode* cond = (SwNode*)stmt->a;
            SwNode* body = (SwNode*)stmt->b;
            int guard = 4096;
            while (guard-- > 0 && !f.ret && !f.discard) {
                if (!sw_boolv(sw_eval(cond, f))) break;
                sw_run_stmt(body, f);
                if (f.brk) { f.brk = false; break; }
                if (f.cont) { f.cont = false; }
                if (f.ret || f.discard) break;
            }
            return;
        }
        case N_RET:
            f.ret = true;
            SWDBG("    N_RET has_a=%d k=%d\\n", stmt->a != 0, stmt->a ? ((SwNode*)stmt->a)->k : -1);
            if (stmt->a) f.retval = sw_eval((SwNode*)stmt->a, f);
            return;
        case N_DISCARD:
            f.discard = true;
            if (f.cv) f.cv->discard = true;
            return;
        case N_BREAK:
            f.brk = true;
            return;
        case N_CONT:
            f.cont = true;
            return;
        default:
            sw_eval(stmt, f);
            return;
    }
}

// run a shader's main() with the given frame seed
static void sw_run_main(SwShader& sh, SwFrame& f) {
    SwNode* main_ = sh.funcs[sh.main_idx];
    if (!main_) return;
    sw_run_stmt((SwNode*)main_->b, f);
}

// ===================== rasterizer =====================
struct SwVtx {
    float x, y, z, w;      // window space
    float vary[SW_MAX_FRAGV * 4];
    int var_t[SW_MAX_FRAGV];
    float attr_w;          // 1/w
};
struct SwDrawCtx {
    SwDrawCtx(SwCanvas& c, SwProg& p) : cv(c), prog(p) { vao = 0; inst = 0; }
    SwCanvas& cv;
    SwProg& prog;
    SwVao* vao;
    int inst;
};

// fetch one attribute component set for vertex index vi (instance-aware)
// fetch one attribute component set for vertex index vi (instance-aware)
static SwVal sw_fetch_attr(SwDrawCtx& dc, int slot, int vi) {
    SwVao& vao = *dc.vao;
    SwVao::SwAttrib& a = vao.attrs[slot];
    SwVal v = sw_val(T_V4);
    if (!a.enabled || a.buf < 0) return v;
    SwBuf_& b = dc.cv.bu[a.buf];
    if (!b.data) return v;
    int idx = a.divisor > 0 ? dc.inst / a.divisor : vi;
    int esz = a.type == SW_FLOAT ? 4 : (a.type == SW_UNSIGNED_SHORT || a.type == SW_SHORT) ? 2 : 1;
    int stride = a.stride > 0 ? a.stride : a.size * esz;
    long off = (long)a.offset + (long)idx * stride;
    if (off < 0 || off + (long)a.size * esz > b.bytes) return v;
    const uint8_t* p = b.data + off;
    SWDBG("    fetch slot=%d vi=%d off=%ld size=%d stride=%d bytes=%d val=", slot, vi, off, a.size, stride, b.bytes);
    int n = a.size > 4 ? 4 : a.size;
    for (int i = 0; i < n; i++) {
        switch (a.type) {
            case SW_FLOAT: v.f[i] = ((const float*)p)[i]; break;
            case SW_UNSIGNED_BYTE: v.f[i] = (float)p[i] / 255.0f; break;
            case SW_BYTE: v.f[i] = (float)((signed char)p[i]) / 127.0f; break;
            case SW_UNSIGNED_SHORT: v.f[i] = (float)((const uint16_t*)p)[i] / 65535.0f; break;
            case SW_SHORT: v.f[i] = (float)((const int16_t*)p)[i] / 32767.0f; break;
            case SW_UNSIGNED_INT: v.f[i] = (float)((const uint32_t*)p)[i] / 4294967295.0f; break;
            default: v.f[i] = 0.0f;
        }
    }
    v.t = a.size <= 1 ? T_F : (a.size == 2 ? T_V2 : (a.size == 3 ? T_V3 : T_V4));
    return v;
}

// run the vertex shader for vertex vi; fill vx
static void sw_run_vertex(SwDrawCtx& dc, int vi, SwVtx& vx) {
    SwCanvas& cv = dc.cv;
    SwShader* vs = &cv.sh[dc.prog.vs];
    // seed attributes
    for (int i = 0; i < dc.prog.attr_n; i++) {
        SwVal a = sw_fetch_attr(dc, dc.prog.attr_slot[i], vi);
        vs->vars[dc.prog.attr_var[i]].val = a;
        SWDBG("  seed attr[%d] var=%d name=%s vi=%d v=%.2f,%.2f,%.2f,%.2f\\n", i, dc.prog.attr_var[i], vs->vars[dc.prog.attr_var[i]].name, vi, a.f[0], a.f[1], a.f[2], a.f[3]);
    }
    cv.gl_pos[0] = cv.gl_pos[1] = cv.gl_pos[2] = 0.0f; cv.gl_pos[3] = 1.0f;
    cv.gl_psize = 1.0f;
    cv.discard = false;
    SwFrame f;
    memset(&f, 0, sizeof(f));
    f.sh = vs;
    f.cv = &cv;
    sw_run_main(*vs, f);
    SWDBG("  VS#%d done: pos=%.3f,%.3f,%.3f,%.3f\\n", vi, cv.gl_pos[0], cv.gl_pos[1], cv.gl_pos[2], cv.gl_pos[3]);
    float pw = cv.gl_pos[3];
    if (pw < 1.0e-6f) { memset(&vx, 0, sizeof(vx)); return; }
    // NDC -> window
    float nx = cv.gl_pos[0] / pw;
    float ny = cv.gl_pos[1] / pw;
    float nz = cv.gl_pos[2] / pw;
    vx.x = (nx * 0.5f + 0.5f) * (float)cv.w;
    vx.y = (1.0f - (ny * 0.5f + 0.5f)) * (float)cv.h;
    vx.z = nz * 0.5f + 0.5f;
    vx.w = pw;
    vx.attr_w = pw > 1.0e-6f ? 1.0f / pw : 1.0f;
    SWDBG("  vx#%d window x=%.1f y=%.1f z=%.3f w=%.2f\n", vi, vx.x, vx.y, vx.z, vx.w);
    for (int i = 0; i < dc.prog.varying_n; i++) {
        int vsi = dc.prog.varying_vs_var[i];
        int fsi = dc.prog.varying_fs_var[i];
        SwVal& val = vs->vars[vsi].val;
        int sz = sw_tsize(val.t);
        if (sz > 4) sz = 4;
        vx.var_t[fsi] = sz;
        for (int k = 0; k < sz; k++) vx.vary[fsi * 4 + k] = val.f[k];
    }
}

static bool sw_depth_test(SwCanvas& cv, float z) {
    if (!cv.depth_test) return true;
    float d = cv.depth[0];
    switch (cv.depth_func) {
        case SW_LESS: return z < d;
        case SW_LEQUAL: return z <= d;
        case SW_GREATER: return z > d;
        case SW_GEQUAL: return z >= d;
        case SW_EQUAL: return z == d;
        case SW_NOTEQUAL: return z != d;
        case SW_ALWAYS: return true;
        case SW_NEVER: return false;
        default: return z <= d;
    }
}

static void sw_blend_pixel(SwCanvas& cv, int x, int y, uint8_t* px, const float* c) {
    float sa = c[3];
    float srcf = 1.0f, dstf = 1.0f;
    if (cv.blend_src == SW_SRC_ALPHA) srcf = sa;
    else if (cv.blend_src == SW_ONE_MINUS_SRC_ALPHA) srcf = 1.0f - sa;
    else if (cv.blend_src == SW_ZERO) srcf = 0.0f;
    if (cv.blend_dst == SW_ONE_MINUS_SRC_ALPHA) dstf = 1.0f - sa;
    else if (cv.blend_dst == SW_SRC_ALPHA) dstf = sa;
    else if (cv.blend_dst == SW_ZERO) dstf = 0.0f;
    float dr = px[0] / 255.0f, dg = px[1] / 255.0f, db = px[2] / 255.0f, da = px[3] / 255.0f;
    float r = c[0] * srcf + dr * dstf;
    float g = c[1] * srcf + dg * dstf;
    float b = c[2] * srcf + db * dstf;
    float a = c[3] * srcf + da * dstf;
    SWDBG("blend x=%d y=%d src=(%.2f,%.2f,%.2f,%.2f) srcf=%.2f dstf=%.2f dst=(%.2f,%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f,%.2f)\n", x, y, c[0], c[1], c[2], c[3], srcf, dstf, dr, dg, db, da, r, g, b, a);
    px[0] = (uint8_t)(r < 0.0f ? 0 : r > 1.0f ? 255 : (int)(r * 255.0f));
    px[1] = (uint8_t)(g < 0.0f ? 0 : g > 1.0f ? 255 : (int)(g * 255.0f));
    px[2] = (uint8_t)(b < 0.0f ? 0 : b > 1.0f ? 255 : (int)(b * 255.0f));
    px[3] = (uint8_t)(a < 0.0f ? 0 : a > 1.0f ? 255 : (int)(a * 255.0f));
}

// interpolate varyings at barycentric (l0,l1,l2) with perspective correction
static void sw_interp_varying(SwDrawCtx& dc, const SwVtx& a, const SwVtx& b, const SwVtx& c,
                              float l0, float l1, float l2) {
    float wsum = a.attr_w * l0 + b.attr_w * l1 + c.attr_w * l2;
    if (wsum == 0.0f) wsum = 1.0f;
    SwCanvas& cv = dc.cv;
    SwShader* fs = &cv.sh[dc.prog.fs];
    for (int i = 0; i < dc.prog.varying_n; i++) {
        int fsi = dc.prog.varying_fs_var[i];
        int sz = a.var_t[fsi];
        if (sz < 1) sz = 1;
        for (int k = 0; k < sz; k++) {
            float v = (a.vary[fsi * 4 + k] * a.attr_w * l0 +
                       b.vary[fsi * 4 + k] * b.attr_w * l1 +
                       c.vary[fsi * 4 + k] * c.attr_w * l2) / wsum;
            fs->vars[fsi].val.f[k] = v;
        }
    }
}

static void sw_run_fragment(SwDrawCtx& dc, int x, int y, float z, float invw) {
    SwCanvas& cv = dc.cv;
    SwShader* fs = &cv.sh[dc.prog.fs];
    cv.gl_fragcoord = sw_vecn(4, (float)x + 0.5f, (float)y + 0.5f, z, invw);
    cv.fragcolor[0] = cv.fragcolor[1] = cv.fragcolor[2] = 0.0f; cv.fragcolor[3] = 1.0f;
    cv.fragdepth = z;
    cv.discard = false;
    SwFrame f;
    memset(&f, 0, sizeof(f));
    f.sh = fs;
    f.cv = &cv;
    sw_run_main(*fs, f);
#ifdef SWGL_TEST_DEBUG
    for (int di = 0; di < fs->vars_n; di++)
        SWDBG("    fsvar[%d] name=%s is_out=%d t=%d val=(%.2f,%.2f,%.2f,%.2f)\n", di, fs->vars[di].name, fs->vars[di].is_out, fs->vars[di].val.t, fs->vars[di].val.f[0], fs->vars[di].val.f[1], fs->vars[di].val.f[2], fs->vars[di].val.f[3]);
#endif
    if (cv.discard) return;
    float* c = cv.fragcolor;
    // output variable (ES3 "out vec4 o")
    for (int i = 0; i < fs->vars_n; i++) {
        if (fs->vars[i].is_out) {
            c = fs->vars[i].val.f;
            break;
        }
    }
    /* per-pixel log off */
    if (x < 0 || y < 0 || x >= cv.w || y >= cv.h) return;
    if (cv.scissor && (x < cv.sc_x || y < cv.sc_y || x >= cv.sc_x + cv.sc_w || y >= cv.sc_y + cv.sc_h))
        return;
    uint8_t* px = cv.fb + ((size_t)y * cv.w + x) * 4;
    if (!sw_depth_test(cv, z)) return;
    float cm[4] = { c[0], c[1], c[2], c[3] };
    if (!cv.color_mask[0]) cm[0] = px[0] / 255.0f;
    if (!cv.color_mask[1]) cm[1] = px[1] / 255.0f;
    if (!cv.color_mask[2]) cm[2] = px[2] / 255.0f;
    if (!cv.color_mask[3]) cm[3] = px[3] / 255.0f;
    if (cv.blend) {
        sw_blend_pixel(cv, x, y, px, cm);
    } else {
        c = cm;
        px[0] = (uint8_t)(c[0] < 0.0f ? 0 : c[0] > 1.0f ? 255 : (int)(c[0] * 255.0f));
        px[1] = (uint8_t)(c[1] < 0.0f ? 0 : c[1] > 1.0f ? 255 : (int)(c[1] * 255.0f));
        px[2] = (uint8_t)(c[2] < 0.0f ? 0 : c[2] > 1.0f ? 255 : (int)(c[2] * 255.0f));
        px[3] = (uint8_t)(c[3] < 0.0f ? 0 : c[3] > 1.0f ? 255 : (int)(c[3] * 255.0f));
    }
    if (cv.depth_mask) cv.depth[(size_t)y * cv.w + x] = z;
}

static void sw_raster_tri(SwDrawCtx& dc, const SwVtx& v0, const SwVtx& v1, const SwVtx& v2) {
    SwCanvas& cv = dc.cv;
    if (v0.w == 0.0f || v1.w == 0.0f || v2.w == 0.0f) return;
    // window-space signed area; inside pixels share the area sign
    float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (area == 0.0f) return;
    bool pos = area > 0.0f;
    if (cv.cull) {
        // NDC CCW (front) maps to area<0 in y-down window space
        bool front = area < 0.0f;
        bool back = !front;
        if (cv.cull_face == SW_BACK && back) return;
        if (cv.cull_face == SW_FRONT && front) return;
    }
    float minx = sw_minf(sw_minf(v0.x, v1.x), v2.x);
    float maxx = sw_maxf(sw_maxf(v0.x, v1.x), v2.x);
    float miny = sw_minf(sw_minf(v0.y, v1.y), v2.y);
    float maxy = sw_maxf(sw_maxf(v0.y, v1.y), v2.y);
    int x0 = (int)sw_floorf(minx), x1 = (int)sw_ceilf(maxx);
    int y0 = (int)sw_floorf(miny), y1 = (int)sw_ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv.w) x1 = cv.w - 1;
    if (y1 >= cv.h) y1 = cv.h - 1;
    float inva = 1.0f / area;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = (float)x, py = (float)y;   // integer pixel grid: NDC (0,0) -> pixel (w/2,h/2)
            float w0 = (v1.x - v0.x) * (py - v0.y) - (v1.y - v0.y) * (px - v0.x);
            float w1 = (v2.x - v1.x) * (py - v1.y) - (v2.y - v1.y) * (px - v1.x);
            float w2 = (v0.x - v2.x) * (py - v2.y) - (v0.y - v2.y) * (px - v2.x);
            // top-left rule, per-edge: an edge with w==0 is owned by the triangle
            // only if the directed edge is top-left; apply independently per edge
            if (pos) {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                if (w0 == 0.0f && !((v1.y - v0.y) < 0.0f || ((v1.y - v0.y) == 0.0f && (v1.x - v0.x) > 0.0f))) continue;
                if (w1 == 0.0f && !((v2.y - v1.y) < 0.0f || ((v2.y - v1.y) == 0.0f && (v2.x - v1.x) > 0.0f))) continue;
                if (w2 == 0.0f && !((v0.y - v2.y) < 0.0f || ((v0.y - v2.y) == 0.0f && (v0.x - v2.x) > 0.0f))) continue;
            } else {
                if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue;
                // CW winding: geometric edge direction is reversed, so ownership flips
                if (w0 == 0.0f && ((v1.y - v0.y) < 0.0f || ((v1.y - v0.y) == 0.0f && (v1.x - v0.x) > 0.0f))) continue;
                if (w1 == 0.0f && ((v2.y - v1.y) < 0.0f || ((v2.y - v1.y) == 0.0f && (v2.x - v1.x) > 0.0f))) continue;
                if (w2 == 0.0f && ((v0.y - v2.y) < 0.0f || ((v0.y - v2.y) == 0.0f && (v0.x - v2.x) > 0.0f))) continue;
            }
                        // edge functions: w0 opposes v0 (edge v1v2), w1 opposes v1 (edge v2v0), w2 opposes v2 (edge v0v1)
            // barycentric weight of vertex i = opposite-edge function / area
            float l0 = w1 * inva;
            float l1 = w2 * inva;
            float l2 = w0 * inva;
            float z = (v0.z * v0.attr_w * l0 + v1.z * v1.attr_w * l1 + v2.z * v2.attr_w * l2) /
                      (v0.attr_w * l0 + v1.attr_w * l1 + v2.attr_w * l2);
            if (z < 0.0f) z = 0.0f;
            if (z > 1.0f) z = 1.0f;
            /* per-pixel log off */
            sw_interp_varying(dc, v0, v1, v2, l0, l1, l2);
            float invw = v0.attr_w * l0 + v1.attr_w * l1 + v2.attr_w * l2;
            sw_run_fragment(dc, x, y, z, invw);
        }
    }
}

static void sw_raster_line(SwDrawCtx& dc, const SwVtx& a, const SwVtx& b) {
    SwCanvas& cv = dc.cv;
    if (a.w == 0.0f || b.w == 0.0f) return;
    float dx = b.x - a.x, dy = b.y - a.y;
    int steps = (int)(sw_absf(dx) > sw_absf(dy) ? sw_absf(dx) : sw_absf(dy));
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float x = a.x + dx * t, y = a.y + dy * t;
        float z = a.z + (b.z - a.z) * t;
        // interpolate varyings linearly (points/lines are not perspective-correct in WebGL either)
        for (int k = 0; k < dc.prog.varying_n; k++) {
            int fsi = dc.prog.varying_fs_var[k];
            int sz = a.var_t[fsi];
            if (sz < 1) sz = 1;
            for (int c = 0; c < sz; c++)
                dc.cv.sh[dc.prog.fs].vars[fsi].val.f[c] = a.vary[fsi * 4 + c] * (1.0f - t) + b.vary[fsi * 4 + c] * t;
        }
        sw_run_fragment(dc, (int)sw_floorf(x), (int)sw_floorf(y), z, 1.0f);
    }
}

static void sw_raster_point(SwDrawCtx& dc, const SwVtx& v) {
    if (v.w == 0.0f) return;
    for (int k = 0; k < dc.prog.varying_n; k++) {
        int fsi = dc.prog.varying_fs_var[k];
        int sz = v.var_t[fsi];
        if (sz < 1) sz = 1;
        for (int c = 0; c < sz; c++) dc.cv.sh[dc.prog.fs].vars[fsi].val.f[c] = v.vary[fsi * 4 + c];
    }
    sw_run_fragment(dc, (int)sw_floorf(v.x), (int)sw_floorf(v.y), v.z, 1.0f);
}

static void sw_emit_prim(SwDrawCtx& dc, int mode, SwVtx* vs, int n, int base, int a, int b, int c) {
    if (mode == SW_POINTS) {    SWDBG("emit_prim mode=%d n=%d\n", mode, n);
        for (int i = 0; i < n; i++) sw_raster_point(dc, vs[base + i]);
        return;
    }
    if (mode == SW_LINES) {
        for (int i = 0; i + 1 < n; i += 2) sw_raster_line(dc, vs[base + i], vs[base + i + 1]);
        return;
    }
    if (mode == SW_LINE_STRIP || mode == SW_LINE_LOOP) {
        int m = mode == SW_LINE_STRIP ? n - 1 : n;
        for (int i = 0; i < m; i++) sw_raster_line(dc, vs[base + i], vs[base + (i + 1) % n]);
        return;
    }
    if (mode == SW_TRIANGLES) {
        for (int i = 0; i + 2 < n; i += 3) sw_raster_tri(dc, vs[base + i], vs[base + i + 1], vs[base + i + 2]);
        return;
    }
    if (mode == SW_TRIANGLE_STRIP) {
        for (int i = 0; i + 2 < n; i++) {
            if (i & 1) sw_raster_tri(dc, vs[base + i + 1], vs[base + i], vs[base + i + 2]);
            else sw_raster_tri(dc, vs[base + i], vs[base + i + 1], vs[base + i + 2]);
        }
        return;
    }
    if (mode == SW_TRIANGLE_FAN) {
        for (int i = 1; i + 1 < n; i++) sw_raster_tri(dc, vs[base], vs[base + i], vs[base + i + 1]);
    }
}

static void sw_do_draw(SwCanvas& cv, int mode, int count, int first, int instCount, bool indexed,
                       int idxType, int idxOffset) {
    SwProg* prog = cv.cur_prog >= 0 ? &cv.pr[cv.cur_prog] : 0;
    if (!prog || !prog->linked) return;
    SwVao* vao = cv.cur_vao >= 0 ? &cv.va[cv.cur_vao] : &cv.def_vao;
    if (count <= 0 || count > 8192) return;
    SWDBG("do_draw linked=%d vao=%p attr_n=%d inst=%d\\n", prog->linked, (void*)vao, prog->attr_n, instCount);
    for (int ai = 0; ai < SW_MAX_ATTRIBS; ai++) if (vao->attrs[ai].enabled) SWDBG("  attr[%d] enabled buf=%d size=%d type=%d stride=%d off=%d div=%d\\n", ai, vao->attrs[ai].buf, vao->attrs[ai].size, vao->attrs[ai].type, vao->attrs[ai].stride, vao->attrs[ai].offset, vao->attrs[ai].divisor);
    SWDBG("  cur_buf=%d cur_elem=%d\\n", cv.cur_buf, cv.cur_elem);
    SwDrawCtx dc(cv, *prog);
    dc.vao = vao;
    SwVtx* vcache = (SwVtx*)kalloc((size_t)count * sizeof(SwVtx));
    if (!vcache) return;
    int* idx = (int*)kalloc((size_t)count * sizeof(int));
    if (!idx) { kfree(vcache); return; }
    for (int inst = 0; inst < instCount; inst++) {
        dc.inst = inst;
        int n = count;
        for (int i = 0; i < n; i++) {
            int vi = first + i;
            if (indexed) {
                SwBuf_* eb = cv.cur_elem >= 0 ? &cv.bu[cv.cur_elem] : 0;
                if (!eb || !eb->data) { kfree(vcache); kfree(idx); return; }
                const uint8_t* p = eb->data + idxOffset;
                if (idxType == SW_UNSIGNED_SHORT) vi = (idxOffset + (long)i * 2 + 2 <= (long)eb->bytes) ? ((const uint16_t*)p)[i] : 0;
                else if (idxType == SW_UNSIGNED_INT) vi = (idxOffset + (long)i * 4 + 4 <= (long)eb->bytes) ? (int)((const uint32_t*)p)[i] : 0;
                else vi = (idxOffset + (long)i + 1 <= (long)eb->bytes) ? p[i] : 0;
            }
            idx[i] = vi;
        }
        for (int i = 0; i < n; i++) {
            int vi = idx[i];
            if (vi < 0) vi = 0;
            sw_run_vertex(dc, vi, vcache[i]);
        }
        SWDBG("calling emit_prim mode=%d n=%d\n", mode, n);
        sw_emit_prim(dc, mode, vcache, n, 0, -1, -1, -1);
    }
}

// ===================== command dispatch =====================
static SwCanvas* sw_canvas_find(const char* cid) {
    for (int i = 0; i < 4; i++)
        if (s_cv[i].used && !strcmp(s_cv[i].cid, cid)) return &s_cv[i];
    return 0;
}
static SwCanvas* sw_canvas_get(const char* cid) {
    SwCanvas* c = sw_canvas_find(cid);
    if (c) return c;
    for (int i = 0; i < 4; i++) {
        if (!s_cv[i].used) {
            memset(&s_cv[i], 0, sizeof(SwCanvas));
            s_cv[i].used = true;
            sw_cp(s_cv[i].cid, sizeof(s_cv[i].cid), cid);
            s_cv[i].w = 300;
            s_cv[i].h = 150;
            s_cv[i].cur_prog = -1;
            s_cv[i].cur_buf = -1;
            s_cv[i].cur_elem = -1;
            s_cv[i].cur_vao = -1;
            s_cv[i].active_unit = 0;
            s_cv[i].depth_func = SW_LEQUAL;
            s_cv[i].blend_src = SW_ONE;
            s_cv[i].blend_dst = SW_ZERO;
            s_cv[i].cull_face = SW_BACK;
            s_cv[i].color_mask[0] = s_cv[i].color_mask[1] = s_cv[i].color_mask[2] = s_cv[i].color_mask[3] = true;
            s_cv[i].depth_mask = true;
            s_cv[i].unpack_alignment = 4;
            for (int u = 0; u < 8; u++) s_cv[i].unit_tex[u] = -1;
            return &s_cv[i];
        }
    }
    return 0;
}

static void sw_canvas_resize(SwCanvas& cv, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 4096) w = 4096;
    if (h > 4096) h = 4096;
    if (cv.fb) kfree(cv.fb);
    if (cv.depth) kfree(cv.depth);
    cv.fb = (uint8_t*)kalloc((size_t)w * h * 4);
    cv.depth = (float*)kalloc((size_t)w * h * sizeof(float));
    if (cv.fb) memset(cv.fb, 0, (size_t)w * h * 4);
    if (cv.depth) memset(cv.depth, 0, (size_t)w * h * sizeof(float));
    cv.w = w;
    cv.h = h;
}

// split args by ','; returns arg count (max 12)
static int sw_split(const char* s, const char** out, int maxn) {
    int n = 0;
    if (!s) return 0;
    while (n < maxn) {
        out[n++] = s;
        const char* c = s;
        while (*c && *c != ',') c++;
        if (!*c) break;
        s = c + 1;
    }
    return n;
}
static int sw_atoi(const char* s) { return (int)sw_atof(s); }

static unsigned sw_parse_sid(const char* s) {
    unsigned v = 0;
    for (const char* p = s; *p && *p != ','; p++) v = v * 10 + (unsigned)(*p - '0');
    return v;
}

// allocate a base64-decoded buffer from val (owned by caller via kalloc)
static uint8_t* sw_decode_data(const char* s, int* outLen) {
    int cap = (int)strlen(s);
    if (cap < 0) cap = 0;
    uint8_t* buf = (uint8_t*)kalloc(cap + 1);
    if (!buf) return 0;
    int n = sw_b64_decode(s, buf, cap);
    *outLen = n;
    return buf;
}

} // namespace (internal helpers)

void swgl_cmd(const char* cid, const char* cmd, const char* val) {
    if (!cid || !cmd || !val) return;
    // decode percent-encoding in place
    char* v = (char*)kalloc(strlen(val) + 1);
    if (!v) return;
    sw_cp(v, strlen(val) + 1, val);
    sw_url_decode(v);
    const char* a[40];
    int an = sw_split(v, a, 40);
    const char* c = cmd;
    if (c[0] == 'g' && c[1] == 'l') c += 2;   // strip "gl" prefix if present

    if (!strcmp(c, "ctx")) {
        SwCanvas* cv = sw_canvas_get(cid);
        if (cv && !cv->fb) sw_canvas_resize(*cv, cv->w, cv->h);
        kfree(v);
        return;
    }
    SwCanvas* cv = sw_canvas_find(cid);
    if (!cv) { kfree(v); return; }

    if (!strcmp(c, "c")) {            // clearColor (wire "glc")
        cv->cr = sw_atof(a[0]); cv->cg = sw_atof(a[1]); cv->cb = sw_atof(a[2]); cv->ca = sw_atof(a[3]);
    } else if (!strcmp(c, "clr")) {   // clear (wire "glclr")
        int bits = sw_atoi(a[0]);
        if (bits & SW_COLOR_BUFFER_BIT) {
            int x0 = 0, y0 = 0, x1 = cv->w, y1 = cv->h;
            if (cv->scissor) { x0 = cv->sc_x; y0 = cv->sc_y; x1 = cv->sc_x + cv->sc_w; y1 = cv->sc_y + cv->sc_h; }
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 > cv->w) x1 = cv->w; if (y1 > cv->h) y1 = cv->h;
            for (int y = y0; y < y1; y++) {
                uint8_t* p = cv->fb + ((size_t)y * cv->w + x0) * 4;
                for (int x = x0; x < x1; x++, p += 4) {
                    if (cv->color_mask[0]) p[0] = (uint8_t)(cv->cr * 255.0f);
                    if (cv->color_mask[1]) p[1] = (uint8_t)(cv->cg * 255.0f);
                    if (cv->color_mask[2]) p[2] = (uint8_t)(cv->cb * 255.0f);
                    if (cv->color_mask[3]) p[3] = (uint8_t)(cv->ca * 255.0f);
                }
            }
        }
        if (bits & SW_DEPTH_BUFFER_BIT) {
            if (cv->depth) {
                float* d = cv->depth;
                size_t dn = (size_t)cv->w * cv->h;
                for (size_t ii = 0; ii < dn; ii++) d[ii] = 1.0f;
            }
        }
    } else if (!strcmp(c, "v")) {     // viewport
        int x = sw_atoi(a[0]), y = sw_atoi(a[1]), w = sw_atoi(a[2]), h = sw_atoi(a[3]);
        (void)x; (void)y; (void)w; (void)h;   // swgl renders to full canvas
    } else if (!strcmp(c, "sh")) {    // shaderSource: sid,type,src (src = rest of string, may contain commas)
        const char* p = v;
        while (*p && *p != ',') p++; if (*p) p++;
        const char* t0 = p;
        while (*p && *p != ',') p++; if (*p) p++;
        const char* src = p;
        char tbuf[24];
        int tn = 0;
        while (t0[tn] && t0[tn] != ',' && tn < 23) { tbuf[tn] = t0[tn]; tn++; }
        tbuf[tn] = 0;
        unsigned sid = sw_atoi(v);
        int type = sw_atoi(tbuf);
        SwShader* sh = sw_shader_of(*cv, type, sid);
        if (!sh) {
            for (int i = 0; i < SW_MAX_SH; i++) {
                if (!cv->sh[i].used) {
                    cv->sh[i].used = true;
                    cv->sh[i].sid = sid;
                    cv->sh[i].type = type;
                    sh = &cv->sh[i];
                    break;
                }
            }
        }
        if (sh) {
            static char srcbuf[8192];
            int o = 0;
            for (const char* q = src; *q && o < 8191; q++) srcbuf[o++] = *q;
            srcbuf[o] = 0;
            sw_sh_compile(*sh, srcbuf);
            SWDBG("sh sid=%u type=%d compiled=%d err=%s src=%.60s\n", sid, type, sh->compiled, sh->err, srcbuf);
        }
    } else if (!strcmp(c, "csh")) {   // compileShader (no-op; compiled at "sh")
    } else if (!strcmp(c, "prog")) {  // createProgram
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_prog_of(*cv, sid))
            for (int i = 0; i < SW_MAX_PROG; i++)
                if (!cv->pr[i].used) { cv->pr[i].used = true; cv->pr[i].sid = sid; cv->pr[i].vs = cv->pr[i].fs = -1; break; }
    } else if (!strcmp(c, "att")) {   // attachShader: pid,sid (JS ids)
        unsigned pid = sw_parse_sid(a[0]);
        unsigned sid = sw_parse_sid(a[1]);
        SwProg* p = sw_prog_of(*cv, pid);
        SwShader* sh = 0;
        for (int i = 0; i < SW_MAX_SH; i++)
            if (cv->sh[i].used && cv->sh[i].sid == sid) { sh = &cv->sh[i]; break; }
        if (p && sh) {
            int si = (int)(sh - cv->sh);
            if (sh->type == SW_VERTEX_SHADER) p->vs = si;
            else p->fs = si;
        }
    } else if (!strcmp(c, "link")) {  // linkProgram
        unsigned pid = sw_parse_sid(a[0]);
        SwProg* p = sw_prog_of(*cv, pid);
        if (p) { sw_prog_link(*cv, *p); SWDBG("link pid=%u -> linked=%d err=%s vs=%d fs=%d vsok=%d fsok=%d\\n", pid, p->linked, p->err, p->vs, p->fs, p->vs>=0&&cv->sh[p->vs].compiled, p->fs>=0&&cv->sh[p->fs].compiled); }
    } else if (!strcmp(c, "use")) {   // useProgram
        unsigned pid = sw_parse_sid(a[0]);
        cv->cur_prog = sw_prog_slot(*cv, pid);
    } else if (!strcmp(c, "bindbuf")) { // bindBuffer: bid,target (JS ids)
        unsigned sid = sw_parse_sid(a[0]);
        int target = sw_atoi(a[1]);
        if (target == SW_ARRAY_BUFFER) cv->cur_buf = sw_buf_slot(*cv, sid);
        else cv->cur_elem = sw_buf_slot(*cv, sid);
    } else if (!strcmp(c, "buf")) {   // bufferData (new form: bid,target,type,usage,b64 | old: bid,target,f1,f2,...)
        unsigned bid = sw_parse_sid(a[0]);
        int target = sw_atoi(a[1]);
        SwBuf_* b = sw_buf_of(*cv, bid);
        if (!b) {
            for (int i = 0; i < SW_MAX_BUF; i++)
                if (!cv->bu[i].used) {
                    cv->bu[i].used = true;
                    cv->bu[i].sid = bid;
                    cv->bu[i].target = target;
                    b = &cv->bu[i];
                    break;
                }
        }
        if (!b) { kfree(v); return; }
        b->target = target;
        if (b->data) { kfree(b->data); b->data = 0; }
        b->bytes = 0;
        if (an >= 5 && (a[2][0] == 'f' || a[2][0] == 'u' || a[2][0] == 'b')) {
            // base64 binary form
            uint8_t* d = sw_decode_data(a[4], &b->bytes);
            if (d) b->data = d;
        } else {
            // legacy float form
            int nf = an - 2;
            if (nf > 0) {
                b->bytes = nf * 4;
                b->data = (uint8_t*)kalloc((size_t)nf * 4);
                if (b->data)
                    for (int i = 2; i < an; i++) ((float*)b->data)[i - 2] = sw_atof(a[i]);
                SWDBG("buf bid=%u nf=%d bytes=%d f0=%.2f f1=%.2f f4=%.2f f5=%.2f\n", bid, nf, b->bytes, b->data?((float*)b->data)[0]:0, b->data?((float*)b->data)[1]:0, b->data?((float*)b->data)[4]:0, b->data?((float*)b->data)[5]:0);
            }
        }
    } else if (!strcmp(c, "bufsub")) { // bufferSubData
        unsigned bid = sw_parse_sid(a[0]);
        int target = sw_atoi(a[1]);
        int offset = sw_atoi(a[2]);
        SwBuf_* b = sw_buf_of(*cv, bid);
        if (!b) { kfree(v); return; }
        int len = 0;
        uint8_t* d = sw_decode_data(a[4], &len);
        if (d && b->data && offset >= 0 && offset + len <= b->bytes) {
            memcpy(b->data + offset, d, len);
        }
        if (d) kfree(d);
    } else if (!strcmp(c, "attptr")) { // vertexAttribPointer: loc,size,stride,offset | loc,size,type,stride,offset
        int loc = sw_atoi(a[0]);
        int size = sw_atoi(a[1]);
        int stride, offset, type;
        if (an >= 5) { type = sw_atoi(a[2]); stride = sw_atoi(a[3]); offset = sw_atoi(a[4]); }
        else { type = SW_FLOAT; stride = sw_atoi(a[2]); offset = sw_atoi(a[3]); }
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot < 0 || slot >= SW_MAX_ATTRIBS) { kfree(v); return; }
        SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &cv->def_vao;
        vao->attrs[slot].enabled = true;
        vao->attrs[slot].buf = cv->cur_buf;
        vao->attrs[slot].size = size;
        vao->attrs[slot].type = type;
        vao->attrs[slot].stride = stride;
        vao->attrs[slot].offset = offset;
    } else if (!strcmp(c, "draw")) {  // drawArrays: mode,count (minijs drops first)
        int mode = sw_atoi(a[0]);
        int count = sw_atoi(a[1]);
        SWDBG("draw mode=%d count=%d cur_prog=%d cur_buf=%d cur_vao=%d\\n", mode, count, cv->cur_prog, cv->cur_buf, cv->cur_vao);
        sw_do_draw(*cv, mode, count, 0, 1, false, 0, 0);
    } else if (!strcmp(c, "deli")) {  // drawElements: mode,count,type,offset
        int mode = sw_atoi(a[0]);
        int count = sw_atoi(a[1]);
        int type = sw_atoi(a[2]);
        int offset = sw_atoi(a[3]);
        sw_do_draw(*cv, mode, count, 0, 1, true, type, offset);
    } else if (!strcmp(c, "drai")) {  // drawArraysInstanced: mode,first,count,inst
        int mode = sw_atoi(a[0]);
        int first = sw_atoi(a[1]);
        int count = sw_atoi(a[2]);
        int inst = sw_atoi(a[3]);
        sw_do_draw(*cv, mode, count, first, inst, false, 0, 0);
    } else if (!strcmp(c, "dri")) {   // drawElementsInstanced: mode,count,type,offset,inst
        int mode = sw_atoi(a[0]);
        int count = sw_atoi(a[1]);
        int type = sw_atoi(a[2]);
        int offset = sw_atoi(a[3]);
        int inst = sw_atoi(a[4]);
        sw_do_draw(*cv, mode, count, 0, inst, true, type, offset);
    } else if (!strcmp(c, "uloc")) {  // getUniformLocation: pid,uid,name
        unsigned pid = sw_parse_sid(a[0]);
        unsigned uid = sw_parse_sid(a[1]);
        const char* name = a[2];
        SwProg* p = sw_prog_of(*cv, pid);
        int slot = -1;
        if (p) {
            for (int i = 0; i < p->uniform_n; i++)
                if (!strcmp(p->uniform_name[i], name)) { slot = i; break; }
        }
        cv->u_loc[uid & 0xFF] = (unsigned)(slot + 1);
    } else if (!strcmp(c, "aloc")) {  // getAttribLocation: pid,aid,name
        unsigned pid = sw_parse_sid(a[0]);
        unsigned aid = sw_parse_sid(a[1]);
        const char* name = a[2];
        SwProg* p = sw_prog_of(*cv, pid);
        int slot = -1;
        if (p) {
            for (int i = 0; i < p->attr_n; i++)
                if (!strcmp(p->attr_name[i], name)) { slot = p->attr_slot[i]; break; }
        }
        cv->a_loc[aid & 0xFF] = (unsigned)(slot + 1);
    } else if (!strcmp(c, "u1f")) {   // uniform1f
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]);
            if (p->uniform_vs[slot] >= 0) cv->sh[p->vs].vars[p->uniform_vs[slot]].val.f[0] = x;
            if (p->uniform_fs[slot] >= 0) cv->sh[p->fs].vars[p->uniform_fs[slot]].val.f[0] = x;
        }
    } else if (!strcmp(c, "u2f")) {
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]), y = sw_atof(a[2]);
            if (p->uniform_vs[slot] >= 0) { SwVal& uv = cv->sh[p->vs].vars[p->uniform_vs[slot]].val; uv.f[0] = x; uv.f[1] = y; }
            if (p->uniform_fs[slot] >= 0) { SwVal& uv = cv->sh[p->fs].vars[p->uniform_fs[slot]].val; uv.f[0] = x; uv.f[1] = y; }
        }
    } else if (!strcmp(c, "u3f")) {
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]), y = sw_atof(a[2]), z = sw_atof(a[3]);
            if (p->uniform_vs[slot] >= 0) { SwVal& uv = cv->sh[p->vs].vars[p->uniform_vs[slot]].val; uv.f[0] = x; uv.f[1] = y; uv.f[2] = z; }
            if (p->uniform_fs[slot] >= 0) { SwVal& uv = cv->sh[p->fs].vars[p->uniform_fs[slot]].val; uv.f[0] = x; uv.f[1] = y; uv.f[2] = z; }
        }
    } else if (!strcmp(c, "u4f")) {
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]), y = sw_atof(a[2]), z = sw_atof(a[3]), w = sw_atof(a[4]);
            if (p->uniform_vs[slot] >= 0) { SwVal& uv = cv->sh[p->vs].vars[p->uniform_vs[slot]].val; uv.f[0] = x; uv.f[1] = y; uv.f[2] = z; uv.f[3] = w; }
            if (p->uniform_fs[slot] >= 0) { SwVal& uv = cv->sh[p->fs].vars[p->uniform_fs[slot]].val; uv.f[0] = x; uv.f[1] = y; uv.f[2] = z; uv.f[3] = w; }
        }
    } else if (!strcmp(c, "uM2") || !strcmp(c, "uM3") || !strcmp(c, "uM4")) {
        int n = c[2] == '2' ? 4 : c[2] == '3' ? 9 : 16;
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n && an >= 1 + n) {
            float vals[16];
            for (int i = 0; i < n; i++) vals[i] = sw_atof(a[1 + i]);
            if (p->uniform_vs[slot] >= 0) { SwVal& uv = cv->sh[p->vs].vars[p->uniform_vs[slot]].val; for (int i = 0; i < n; i++) uv.f[i] = vals[i]; }
            if (p->uniform_fs[slot] >= 0) { SwVal& uv = cv->sh[p->fs].vars[p->uniform_fs[slot]].val; for (int i = 0; i < n; i++) uv.f[i] = vals[i]; }
        }
    } else if (!strcmp(c, "u4i")) {   // uniform4iv-ish (samplers come as u1i)
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]);
            if (p->uniform_vs[slot] >= 0) cv->sh[p->vs].vars[p->uniform_vs[slot]].val.f[0] = x;
            if (p->uniform_fs[slot] >= 0) cv->sh[p->fs].vars[p->uniform_fs[slot]].val.f[0] = x;
        }
    } else if (!strcmp(c, "u1i") || !strcmp(c, "u2i") || !strcmp(c, "u3i")) {
        int proxy = sw_atoi(a[0]);
        int slot = (int)cv->u_loc[proxy & 0xFF] - 1;
        SwProg* p = cv->cur_prog >= 0 ? &cv->pr[cv->cur_prog] : 0;
        if (p && slot >= 0 && slot < p->uniform_n) {
            float x = sw_atof(a[1]);
            if (p->uniform_vs[slot] >= 0) cv->sh[p->vs].vars[p->uniform_vs[slot]].val.f[0] = x;
            if (p->uniform_fs[slot] >= 0) cv->sh[p->fs].vars[p->uniform_fs[slot]].val.f[0] = x;
        }
    } else if (!strcmp(c, "enable") || !strcmp(c, "en")) {
        int cap = sw_atoi(a[0]);
        if (cap == SW_DEPTH_TEST) cv->depth_test = true;
        else if (cap == SW_BLEND) cv->blend = true;
        else if (cap == SW_CULL_FACE) cv->cull = true;
        else if (cap == SW_SCISSOR_TEST) cv->scissor = true;
    } else if (!strcmp(c, "dis")) {
        int cap = sw_atoi(a[0]);
        if (cap == SW_DEPTH_TEST) cv->depth_test = false;
        else if (cap == SW_BLEND) cv->blend = false;
        else if (cap == SW_CULL_FACE) cv->cull = false;
        else if (cap == SW_SCISSOR_TEST) cv->scissor = false;
    } else if (!strcmp(c, "blend")) { // blendFunc: src,dst
        cv->blend_src = sw_atoi(a[0]);
        cv->blend_dst = sw_atoi(a[1]);
    } else if (!strcmp(c, "dfunc")) { // depthFunc
        cv->depth_func = sw_atoi(a[0]);
    } else if (!strcmp(c, "cull")) {  // cullFace
        cv->cull_face = sw_atoi(a[0]);
    } else if (!strcmp(c, "scis")) {  // scissor: x,y,w,h
        cv->sc_x = sw_atoi(a[0]); cv->sc_y = sw_atoi(a[1]);
        cv->sc_w = sw_atoi(a[2]); cv->sc_h = sw_atoi(a[3]);
    } else if (!strcmp(c, "cmask")) { // colorMask
        cv->color_mask[0] = sw_atoi(a[0]) != 0;
        cv->color_mask[1] = sw_atoi(a[1]) != 0;
        cv->color_mask[2] = sw_atoi(a[2]) != 0;
        cv->color_mask[3] = sw_atoi(a[3]) != 0;
    } else if (!strcmp(c, "dmask")) { // depthMask
        cv->depth_mask = sw_atoi(a[0]) != 0;
    } else if (!strcmp(c, "pixi")) {  // pixelStorei
        int pname = sw_atoi(a[0]);
        int param = sw_atoi(a[1]);
        if (pname == SW_UNPACK_ALIGNMENT) cv->unpack_alignment = param;
        else if (pname == SW_UNPACK_FLIP_Y) cv->unpack_flip_y = param;
    } else if (!strcmp(c, "actex")) { // activeTexture
        int unit = sw_atoi(a[0]);
        if (unit >= SW_TEXTURE0) unit -= SW_TEXTURE0;
        if (unit < 0) unit = 0;
        if (unit > 7) unit = 7;
        cv->active_unit = unit;
    } else if (!strcmp(c, "texp")) {  // texParameteri: target,pname,param
        int pname = sw_atoi(a[1]);
        int param = sw_atoi(a[2]);
        int ti = cv->unit_tex[cv->active_unit];
        if (ti >= 0) {
            SwTex& t = cv->tx[ti];
            if (pname == SW_TEXTURE_MIN_FILTER) t.min_filter = param;
            else if (pname == SW_TEXTURE_MAG_FILTER) t.mag_filter = param;
            else if (pname == SW_TEXTURE_WRAP_S) t.wrap_s = param;
            else if (pname == SW_TEXTURE_WRAP_T) t.wrap_t = param;
        }
    } else if (!strcmp(c, "texim")) { // texImage2D (no data): target,level,internal,w,h,border,format,type
        int internal = sw_atoi(a[2]);
        int w = sw_atoi(a[3]), h = sw_atoi(a[4]);
        int format = sw_atoi(a[6]);
        int type = sw_atoi(a[7]);
        int ti = cv->unit_tex[cv->active_unit];
        if (ti >= 0) {
            SwTex& t = cv->tx[ti];
            t.internal = internal;
            t.format = format;
            t.data_type = type;
            t.w = w; t.h = h;
        }
    } else if (!strcmp(c, "teximg")) { // texImage2D with data: target,level,internal,w,h,border,format,type,b64
        int internal = sw_atoi(a[2]);
        int w = sw_atoi(a[3]), h = sw_atoi(a[4]);
        int format = sw_atoi(a[6]);
        int type = sw_atoi(a[7]);
        int ti = cv->unit_tex[cv->active_unit];
        if (ti >= 0) {
            SwTex& t = cv->tx[ti];
            t.internal = internal;
            t.format = format;
            t.data_type = type;
            t.w = w; t.h = h;
            if (t.rgba) { kfree(t.rgba); t.rgba = 0; }
            int len = 0;
            uint8_t* d = sw_decode_data(a[8], &len);
            if (d) {
                t.rgba = (uint8_t*)kalloc((size_t)w * h * 4);
                if (t.rgba) {
                    memset(t.rgba, 0, (size_t)w * h * 4);
                    int srcBytes = format == SW_RGBA ? 4 : format == SW_RGB ? 3 :
                                   format == SW_LUMINANCE_ALPHA ? 2 : 1;
                    for (int y = 0; y < h && y * srcBytes * w < len; y++) {
                        for (int x = 0; x < w; x++) {
                            const uint8_t* s = d + (y * w + x) * srcBytes;
                            uint8_t* o = t.rgba + (y * w + x) * 4;
                            switch (format) {
                                case SW_RGBA: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; break;
                                case SW_RGB: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
                                case SW_LUMINANCE: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
                                case SW_ALPHA: o[0] = o[1] = o[2] = 0; o[3] = s[0]; break;
                                case SW_LUMINANCE_ALPHA: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
                                default: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
                            }
                        }
                    }
                }
                kfree(d);
            }
        }
    } else if (!strcmp(c, "texsub")) { // texSubImage2D: target,level,x,y,w,h,format,type,b64
        int x0 = sw_atoi(a[2]), y0 = sw_atoi(a[3]);
        int w = sw_atoi(a[4]), h = sw_atoi(a[5]);
        int format = sw_atoi(a[6]);
        int ti = cv->unit_tex[cv->active_unit];
        if (ti >= 0) {
            SwTex& t = cv->tx[ti];
            int len = 0;
            uint8_t* d = sw_decode_data(a[8], &len);
            if (d && t.rgba) {
                int srcBytes = format == SW_RGBA ? 4 : format == SW_RGB ? 3 :
                               format == SW_LUMINANCE_ALPHA ? 2 : 1;
                for (int y = 0; y < h && y0 + y < t.h; y++) {
                    for (int x = 0; x < w && x0 + x < t.w; x++) {
                        const uint8_t* s = d + (y * w + x) * srcBytes;
                        uint8_t* o = t.rgba + ((y0 + y) * t.w + x0 + x) * 4;
                        switch (format) {
                            case SW_RGBA: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; break;
                            case SW_RGB: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
                            case SW_LUMINANCE: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
                            case SW_ALPHA: o[0] = o[1] = o[2] = 0; o[3] = s[0]; break;
                            default: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
                        }
                    }
                }
                kfree(d);
            } else if (d) kfree(d);
        }
    } else if (!strcmp(c, "btx")) {   // bindTexture: target,sid
        unsigned sid = sw_parse_sid(a[1]);
        int ti = sw_tex_slot(*cv, sid);
        if (ti < 0) {
            for (int i = 0; i < SW_MAX_TEX; i++)
                if (!cv->tx[i].used) {
                    cv->tx[i].used = true;
                    cv->tx[i].sid = sid;
                    cv->tx[i].target = SW_TEXTURE_2D;
                    cv->tx[i].min_filter = SW_NEAREST;
                    cv->tx[i].mag_filter = SW_NEAREST;
                    cv->tx[i].wrap_s = SW_REPEAT;
                    cv->tx[i].wrap_t = SW_REPEAT;
                    ti = i;
                    break;
                }
        }
        if (ti >= 0) cv->unit_tex[cv->active_unit] = ti;
    } else if (!strcmp(c, "vao")) {   // createVertexArray
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_vao_of(*cv, sid))
            for (int i = 0; i < SW_MAX_VAO; i++)
                if (!cv->va[i].used) { cv->va[i].used = true; cv->va[i].sid = sid; cv->va[i].elem_buf = -1; break; }
    } else if (!strcmp(c, "tx")) {    // createTexture
        unsigned sid = sw_parse_sid(a[0]);
        if (!sw_tex_of(*cv, sid))
            for (int i = 0; i < SW_MAX_TEX; i++)
                if (!cv->tx[i].used) { cv->tx[i].used = true; cv->tx[i].sid = sid; cv->tx[i].target = SW_TEXTURE_2D; cv->tx[i].min_filter = SW_NEAREST; cv->tx[i].mag_filter = SW_NEAREST; cv->tx[i].wrap_s = SW_REPEAT; cv->tx[i].wrap_t = SW_REPEAT; break; }
    } else if (!strcmp(c, "bva")) {   // bindVertexArray
        unsigned sid = sw_parse_sid(a[0]);
        if (sid == 0) {
            cv->cur_vao = -1;   // default
        } else {
            int slot = sw_vao_slot(*cv, sid);
            if (slot >= 0) cv->cur_vao = slot;
        }
    } else if (!strcmp(c, "div")) {   // vertexAttribDivisor
        int loc = sw_atoi(a[0]);
        int divisor = sw_atoi(a[1]);
        int slot = loc;
        if (cv->a_loc[loc & 0xFF]) slot = (int)cv->a_loc[loc & 0xFF] - 1;
        if (slot >= 0 && slot < SW_MAX_ATTRIBS) {
            SwVao* vao = cv->cur_vao >= 0 ? &cv->va[cv->cur_vao] : &cv->def_vao;
            vao->attrs[slot].divisor = divisor;
        }
    } else if (!strcmp(c, "del")) {   // delete: kind,sid
        char kind = a[0][0];
        unsigned sid = sw_parse_sid(a[1]);
        if (kind == 'b') {
            for (int i = 0; i < SW_MAX_BUF; i++)
                if (cv->bu[i].used && cv->bu[i].sid == sid) {
                    if (cv->bu[i].data) kfree(cv->bu[i].data);
                    memset(&cv->bu[i], 0, sizeof(cv->bu[i]));
                }
        } else if (kind == 't') {
            for (int i = 0; i < SW_MAX_TEX; i++)
                if (cv->tx[i].used && cv->tx[i].sid == sid) {
                    if (cv->tx[i].rgba) kfree(cv->tx[i].rgba);
                    memset(&cv->tx[i], 0, sizeof(cv->tx[i]));
                }
        } else if (kind == 's') {
            for (int i = 0; i < SW_MAX_SH; i++)
                if (cv->sh[i].used && cv->sh[i].sid == sid) {
                    if (cv->sh[i].arena) sw_afree(cv->sh[i].arena);
                    memset(&cv->sh[i], 0, sizeof(cv->sh[i]));
                }
        } else if (kind == 'p') {
            for (int i = 0; i < SW_MAX_PROG; i++)
                if (cv->pr[i].used && cv->pr[i].sid == sid) memset(&cv->pr[i], 0, sizeof(cv->pr[i]));
        } else if (kind == 'v') {
            for (int i = 0; i < SW_MAX_VAO; i++)
                if (cv->va[i].used && cv->va[i].sid == sid) memset(&cv->va[i], 0, sizeof(cv->va[i]));
        }
    } else if (!strcmp(c, "resize")) {
        int w = sw_atoi(a[0]), h = sw_atoi(a[1]);
        sw_canvas_resize(*cv, w, h);
    }
    kfree(v);
}

// ===================== public API =====================
const uint8_t* swgl_frame(const char* cid, int* w, int* h) {
    SwCanvas* c = sw_canvas_find(cid);
    if (!c || !c->fb) return 0;
    if (w) *w = c->w;
    if (h) *h = c->h;
    return c->fb;
}
void swgl_resize(const char* cid, int w, int h) {
    SwCanvas* c = sw_canvas_find(cid);
    if (c) sw_canvas_resize(*c, w, h);
}
void swgl_shutdown() {
    for (int i = 0; i < 4; i++) {
        if (!s_cv[i].used) continue;
        for (int j = 0; j < SW_MAX_BUF; j++)
            if (s_cv[i].bu[j].data) kfree(s_cv[i].bu[j].data);
        for (int j = 0; j < SW_MAX_TEX; j++)
            if (s_cv[i].tx[j].rgba) kfree(s_cv[i].tx[j].rgba);
        for (int j = 0; j < SW_MAX_SH; j++)
            if (s_cv[i].sh[j].arena) sw_afree(s_cv[i].sh[j].arena);
        if (s_cv[i].fb) kfree(s_cv[i].fb);
        if (s_cv[i].depth) kfree(s_cv[i].depth);
        memset(&s_cv[i], 0, sizeof(SwCanvas));
    }
}
bool swgl_active() {
    for (int i = 0; i < 4; i++)
        if (s_cv[i].used) return true;
    return false;
}

} // namespace nefu