// swgl unit test - compiles swgl.cpp standalone on Windows (MinGW) and verifies
// the software WebGL2 rasterizer pixel-by-pixel: clear/depth, textures,
// instancing, blending, scissor, color masks and GLSL control flow.
//
// Build:  g++ -std=c++17 -O2 -I../core/apps ../core/apps/swgl.cpp swgl_test.cpp -o swgl_test.exe
// Run:    swgl_test.exe   (exit code 0 = all checks passed)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---- klib/platform stubs (host test build only) ----
extern "C" {
void* memcpy(void* d, const void* s, size_t n) { unsigned char* dd = (unsigned char*)d; const unsigned char* ss = (const unsigned char*)s; for (size_t i = 0; i < n; i++) dd[i] = ss[i]; return d; }
void* memmove(void* d, const void* s, size_t n) { unsigned char* dd = (unsigned char*)d; const unsigned char* ss = (const unsigned char*)s; if (dd < ss) { for (size_t i = 0; i < n; i++) dd[i] = ss[i]; } else if (dd > ss) { for (size_t i = n; i > 0; i--) dd[i - 1] = ss[i - 1]; } return d; }
void* memset(void* d, int c, size_t n) { unsigned char* dd = (unsigned char*)d; for (size_t i = 0; i < n; i++) dd[i] = (unsigned char)c; return d; }
int   memcmp(const void* a, const void* b, size_t n) { const unsigned char* aa = (const unsigned char*)a; const unsigned char* bb = (const unsigned char*)b; for (size_t i = 0; i < n; i++) { if (aa[i] != bb[i]) return aa[i] - bb[i]; } return 0; }
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int   strcmp(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int   strncmp(const char* a, const char* b, size_t n) { for (size_t i = 0; i < n; i++) { if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i]; } return 0; }
char* strcpy(char* d, const char* s) { char* r = d; while ((*d++ = *s++)) {} return r; }
char* strncpy(char* d, const char* s, size_t n) { char* r = d; for (size_t i = 0; i < n; i++) { if (*s) { *d++ = *s++; } else *d++ = 0; } return r; }
char* strcat(char* d, const char* s) { char* r = d; while (*d) d++; while ((*d++ = *s++)) {} return r; }
char* strchr(const char* s, int c) { while (*s) { if (*s == (char)c) return (char*)s; s++; } return (char)c == 0 ? (char*)s : 0; }
char* strrchr(const char* s, int c) { const char* last = 0; while (*s) { if (*s == (char)c) last = s; s++; } if ((char)c == 0) return (char*)s; return (char*)last; }
char* strstr(const char* h, const char* n) { if (!*n) return (char*)h; for (; *h; h++) { const char* a = h; const char* b = n; while (*b && *a == *b) { a++; b++; } if (!*b) return (char*)h; } return 0; }
int   atoi(const char* s) { int v = 0; int sg = 1; while (*s == ' ' || *s == '\t') s++; if (*s == '-') { sg = -1; s++; } else if (*s == '+') s++; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; } return v * sg; }
}namespace nefu {
void* kalloc(size_t n) { return ::malloc(n ? n : 1); }
void  kfree(void* p) { ::free(p); }
void* krealloc(void* p, size_t n) { return ::realloc(p, n); }
}
void* operator new(size_t sz) { return ::malloc(sz ? sz : 1); }
void* operator new[](size_t sz) { return ::malloc(sz ? sz : 1); }
void  operator delete(void* p) noexcept { ::free(p); }
void  operator delete[](void* p) noexcept { ::free(p); }
int   ksprintf(char*, size_t, const char*, ...) { return 0; }
void  klogf(const char*, ...) {}

#include "../core/apps/swgl.h"

// ---- helpers ----
static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static std::string urlenc(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
        else {
            char b[8];
            snprintf(b, sizeof(b), "%%%02X", c);
            r += b;
        }
    }
    return r;
}
static std::string b64(const uint8_t* d, int n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        r += T[(v >> 18) & 63];
        r += T[(v >> 12) & 63];
        r += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        r += (i + 2 < n) ? T[v & 63] : '=';
    }
    return r;
}
static void gl(const std::string& cid, const std::string& cmd, const std::string& val) {
    nefu::swgl_cmd(cid.c_str(), cmd.c_str(), val.c_str());
}
static const uint8_t* frame(const char* cid, int* w, int* h) {
    return nefu::swgl_frame(cid, w, h);
}

// shader source with #version etc. url-encoded as the real bridge would do
static void shader(int sid, int type, const std::string& src) {
    char v[8192];
    snprintf(v, sizeof(v), "%d,%d,%s", sid, type, urlenc(src).c_str());
    gl("c0", "glsh", v);
    char c[32];
    snprintf(c, sizeof(c), "%d", sid);
    gl("c0", "glcsh", c);
}
static void prog_link(int pid) {
    char v[32];
    snprintf(v, sizeof(v), "%d", pid);
    gl("c0", "glprog", v);
    // attach vs=1 fs=2 for a two-shader program
    char a1[32], a2[32];
    snprintf(a1, sizeof(a1), "%d,1", pid);
    snprintf(a2, sizeof(a2), "%d,2", pid);
    gl("c0", "glatt", a1);
    gl("c0", "glatt", a2);
    gl("c0", "gllink", v);
    gl("c0", "gluse", v);
}
static void buf_data(int bid, int target, const uint8_t* d, int n) {
    char v[65536];
    snprintf(v, sizeof(v), "%d,%d,f,35044,%s", bid, target, b64(d, n).c_str());
    gl("c0", "glbuf", v);
    char b[32];
    snprintf(b, sizeof(b), "%d,%d", bid, target);
    gl("c0", "glbindbuf", b);
}
static void tex_data(int tid, int w, int h, const uint8_t* rgba) {
    char v[65536];
    char b[32];
    snprintf(b, sizeof(b), "%d,3553", tid);
    gl("c0", "glbtx", b);   // bindTexture: sid,target
    snprintf(v, sizeof(v), "3553,0,32856,%d,%d,0,6408,5121,%s", w, h, b64(rgba, w * h * 4).c_str());
    gl("c0", "glteximg", v);
}

// pixel fetch helper
static bool px(const char* cid, int x, int y, int* r, int* g, int* b, int* a) {
    int w = 0, h = 0;
    const uint8_t* f = frame(cid, &w, &h);
    if (!f || x < 0 || y < 0 || x >= w || y >= h) return false;
    const uint8_t* p = f + ((size_t)y * w + x) * 4;
    *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3];
    return true;
}

// ===================== common textured-quad shaders =====================
static const char* VS_QUAD = R"GLSL(
#version 300 es
in vec2 aPos;
in vec2 aUV;
uniform mat4 uM;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uM * vec4(aPos, 0.0, 1.0);
}
)GLSL";
static const char* FS_TEX = R"GLSL(
#version 300 es
precision mediump float;
in vec2 vUV;
out vec4 o;
uniform sampler2D uT;
void main() {
    o = texture(uT, vUV);
}
)GLSL";
static const char* FS_RED = R"GLSL(
#version 300 es
precision mediump float;
out vec4 o;
void main() {
    o = vec4(1.0, 0.0, 0.0, 1.0);
}
)GLSL";

// identity-ish perspective matrix (keeps z, no projection tricks)
static float g_mat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
static void set_mat_zt(float z) {  // scale z by s (identity otherwise)
    g_mat[0]=1; g_mat[1]=0; g_mat[2]=0; g_mat[3]=0;
    g_mat[4]=0; g_mat[5]=1; g_mat[6]=0; g_mat[7]=0;
    g_mat[8]=0; g_mat[9]=0; g_mat[10]=1; g_mat[11]=0;
    g_mat[12]=0; g_mat[13]=0; g_mat[14]=z; g_mat[15]=1;  // column-major translate z
}

int main() {
    // ---------- setup ----------
    gl("c0", "glctx", "");
    gl("c0", "resize", "64,64");
    int w = 0, h = 0;
    CHECK(frame("c0", &w, &h) != 0 && w == 64 && h == 64, "ctx+resize");

    // quad geometry: two triangles, position + uv interleaved (8 floats per vertex)
    float quad[] = {
        -1,-1, 0,0,   1,-1, 1,0,   1,1, 1,1,
        -1,-1, 0,0,   1,1, 1,1,   -1,1, 0,1,
    };
    buf_data(10, 34962, (const uint8_t*)quad, sizeof(quad));

    // 4x4 texture: texel (2,2) red, (1,1) green, rest white
    uint8_t tex[16 * 4];
    for (int i = 0; i < 16; i++) { tex[i*4]=255; tex[i*4+1]=255; tex[i*4+2]=255; tex[i*4+3]=255; }
    tex[(2*4+2)*4+0] = 255; tex[(2*4+2)*4+1] = 0; tex[(2*4+2)*4+2] = 0;
    tex[(1*4+1)*4+0] = 0; tex[(1*4+1)*4+1] = 255; tex[(1*4+1)*4+2] = 0;
    tex_data(20, 4, 4, tex);
    gl("c0", "actex", "33984");   // TEXTURE0

    shader(1, 35633, VS_QUAD);    // VERTEX_SHADER
    shader(2, 35632, FS_TEX);     // FRAGMENT_SHADER
    prog_link(5);
    gl("c0", "glaloc", "5,101,aPos");
    gl("c0", "glaloc", "5,102,aUV");
    gl("c0", "gluloc", "5,103,uM");
    gl("c0", "gluloc", "5,104,uT");
    gl("c0", "glattptr", "101,2,5126,16,0");
    gl("c0", "glattptr", "102,2,5126,16,8");
    gl("c0", "glu1i", "104,0");

    // ---------- Test 1: clear + depth + textured quad ----------
    gl("c0", "glc", "0.2,0.2,0.2,1");
    gl("c0", "glclr", "16640");          // COLOR|DEPTH
    gl("c0", "enable", "2929");          // DEPTH_TEST
    gl("c0", "dfunc", "515");            // LEQUAL
    char m[160];
    set_mat_zt(0.0f);
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", g_mat[i]);
    gl("c0", "gluM4", std::string("103,") + m);
    gl("c0", "gltexp", "3553,10241,9728");   // MIN_FILTER NEAREST
    gl("c0", "gltexp", "3553,10240,9728");   // MAG_FILTER NEAREST
    gl("c0", "draw", "4,6");
    int r=0,g=0,b=0,a=0;
    px("c0", 32, 32, &r, &g, &b, &a);
    CHECK(r == 255 && g == 0 && b == 0, "textured quad center is red texel");
    px("c0", 4, 4, &r, &g, &b, &a);
    CHECK(r == 255 && g == 255 && b == 255, "corner samples white texel");

    // ---------- Test 2: depth test keeps near quad ----------
    shader(3, 35632, FS_RED);
    // program 6 = vs 1 + fs 3
    {
        char v[32];
        snprintf(v, sizeof(v), "%d", 6);
        gl("c0", "glprog", v);
        char a1[32]; snprintf(a1, sizeof(a1), "%d,1", 6); gl("c0", "glatt", a1);
        char a2[32]; snprintf(a2, sizeof(a2), "%d,2", 6); gl("c0", "glatt", a2);   // re-attach fs 3
        gl("c0", "glatt", a2);   // attach fs=3? fix below
    }
    // rebuild properly: program 6 = vs1 + fs3
    gl("c0", "glprog", "6");
    gl("c0", "glatt", "6,1");
    gl("c0", "glatt", "6,3");
    gl("c0", "gllink", "6");
    gl("c0", "gluse", "6");
    gl("c0", "glaloc", "6,105,aPos");
    gl("c0", "gluloc", "6,106,uM");
    gl("c0", "glattptr", "105,2,5126,16,0");
    set_mat_zt(0.9f);
    memset(m, 0, sizeof(m));
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", g_mat[i]);
    gl("c0", "gluM4", std::string("106,") + m);
    gl("c0", "draw", "4,6");
    px("c0", 32, 32, &r, &g, &b, &a);
    CHECK(r == 255 && g == 0 && b == 0, "near quad (z=0) wins depth test over z=0.9 red quad");

    // ---------- Test 3: instancing (divisor) ----------
    static const char* VS_INST = R"GLSL(
#version 300 es
in vec2 aPos;
in vec2 aUV;
in float aInst;
uniform mat4 uM;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uM * vec4(aPos + vec2(aInst, 0.0), 0.0, 1.0);
}
)GLSL";
    shader(4, 35633, VS_INST);
    gl("c0", "glprog", "7");
    gl("c0", "glatt", "7,4");
    gl("c0", "glatt", "7,2");
    gl("c0", "gllink", "7");
    gl("c0", "gluse", "7");
    gl("c0", "glaloc", "7,107,aPos");
    gl("c0", "glaloc", "7,108,aUV");
    gl("c0", "glaloc", "7,109,aInst");
    gl("c0", "gluloc", "7,110,uM");
    gl("c0", "gluloc", "7,111,uT");
    gl("c0", "glu1i", "111,0");
    gl("c0", "glattptr", "107,2,5126,16,0");
    gl("c0", "glattptr", "108,2,5126,16,8");
    // aInst: per-instance offset (divisor 1)
    float inst[2] = { -0.6f, 0.6f };
    buf_data(11, 34962, (const uint8_t*)inst, sizeof(inst));
    gl("c0", "glbindbuf", "11,34962");
    gl("c0", "glattptr", "109,1,5126,4,0");
    gl("c0", "glbindbuf", "10,34962");
    gl("c0", "div", "109,1");
    gl("c0", "glc", "0.1,0.1,0.1,1");
    gl("c0", "glclr", "16640");
    gl("c0", "enable", "2884");            // CULL_FACE
    gl("c0", "cull", "1029");              // BACK
    set_mat_zt(0.0f);
    memset(m, 0, sizeof(m));
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", g_mat[i]);
    gl("c0", "gluM4", std::string("110,") + m);
    gl("c0", "drai", "4,0,6,2");
    int rx=0, gx=0;
    px("c0", 16, 32, &r, &g, &b, &a);
    rx = r;
    px("c0", 48, 32, &r, &g, &b, &a);
    gx = r;
    CHECK(rx == 255 && gx == 255, "instancing draws two textured quads (left+right)");

    // ---------- Test 4: blending ----------
    static const char* FS_ALPHA = R"GLSL(
#version 300 es
precision mediump float;
out vec4 o;
void main() { o = vec4(0.0, 0.0, 0.0, 0.5); }
)GLSL";
    shader(5, 35632, FS_ALPHA);
    gl("c0", "glprog", "8");
    gl("c0", "glatt", "8,1");
    gl("c0", "glatt", "8,5");
    gl("c0", "gllink", "8");
    gl("c0", "gluse", "8");
    gl("c0", "glaloc", "8,112,aPos");
    gl("c0", "gluloc", "8,113,uM");
    gl("c0", "glattptr", "112,2,5126,16,0");
    gl("c0", "glc", "1,1,1,1");
    gl("c0", "glclr", "16640");
    gl("c0", "enable", "3042");            // BLEND
    gl("c0", "blend", "770,771");          // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    set_mat_zt(0.0f);
    memset(m, 0, sizeof(m));
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", g_mat[i]);
    gl("c0", "gluM4", std::string("113,") + m);
    gl("c0", "draw", "4,6");
    px("c0", 32, 32, &r, &g, &b, &a);
    CHECK(r == 127 && g == 127 && b == 127, "50% black over white blends to 127");

    // ---------- Test 5: scissor + color mask ----------
    gl("c0", "glc", "0,1,0,1");            // green clear
    gl("c0", "enable", "3089");            // SCISSOR_TEST
    gl("c0", "scis", "0,0,32,64");
    gl("c0", "cmask", "1,1,1,1");
    gl("c0", "glclr", "16640");
    px("c0", 8, 32, &r, &g, &b, &a);
    CHECK(g == 255 && r == 0, "scissored clear colors left half green");
    px("c0", 40, 32, &r, &g, &b, &a);
    CHECK(g != 255, "right half untouched by scissored clear");
    gl("c0", "dis", "3089");
    gl("c0", "cmask", "1,0,0,1");          // only red channel writable
    gl("c0", "glc", "1,1,0,1");
    gl("c0", "glclr", "16640");
    px("c0", 40, 32, &r, &g, &b, &a);
    CHECK(r == 255 && g == 127 && b == 127, "color mask preserves green/blue while clearing red");

    // ---------- Test 6: GLSL control flow (for, function, smoothstep, swizzle) ----------
    static const char* FS_FLOW = R"GLSL(
#version 300 es
precision mediump float;
out vec4 o;
float h(float x) { return x * 2.0; }
void main() {
    float acc = 0.0;
    for (int i = 0; i < 4; i++) { acc += float(i); }
    float s = smoothstep(0.2, 0.8, 0.5);
    vec3 v = vec3(1.0, 0.5, 0.25);
    o = vec4(h(acc) / 12.0, s, v.zy, 1.0);
}
)GLSL";
    shader(6, 35632, FS_FLOW);
    gl("c0", "cmask", "1,1,1,1");   // reset color mask before Test 6
    gl("c0", "dis", "3042");   // disable BLEND (left over from Test 4)
    gl("c0", "glprog", "9");
    gl("c0", "glatt", "9,1");
    gl("c0", "glatt", "9,6");
    gl("c0", "gllink", "9");
    gl("c0", "gluse", "9");
    gl("c0", "glaloc", "9,114,aPos");
    gl("c0", "gluloc", "9,115,uM");
    gl("c0", "glattptr", "114,2,5126,16,0");
    gl("c0", "glc", "0,0,0,1");
    gl("c0", "glclr", "16640");
    set_mat_zt(0.0f);
    memset(m, 0, sizeof(m));
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", g_mat[i]);
    gl("c0", "gluM4", std::string("115,") + m);
    gl("c0", "draw", "4,6");
    // o = (h(6)/12, smoothstep(0.2,0.8,0.5), v.z, v.y) = (1.0, 0.5, 0.25, 0.5)
    px("c0", 32, 32, &r, &g, &b, &a);
    CHECK(r == 255 && g == 127 && b == 63 && a == 127, "GLSL for/function/smoothstep/swizzle");

    nefu::swgl_shutdown();
    printf("swgl_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
