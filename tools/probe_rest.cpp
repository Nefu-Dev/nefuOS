// probe tests 4/5/6: blend, colormask, control flow
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
static std::string urlenc(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
        else { char b[8]; snprintf(b, sizeof(b), "%%%02X", c); r += b; }
    }
    return r;
}
static void gl(const std::string& cid, const std::string& cmd, const std::string& val) { nefu::swgl_cmd(cid.c_str(), cmd.c_str(), val.c_str()); }
static const uint8_t* frame(const char* cid, int* w, int* h) { return nefu::swgl_frame(cid, w, h); }
static void shader(int sid, int type, const std::string& src) {
    char v[8192];
    snprintf(v, sizeof(v), "%d,%d,%s", sid, type, urlenc(src).c_str());
    gl("c0", "glsh", v);
    char c[32]; snprintf(c, sizeof(c), "%d", sid); gl("c0", "glcsh", c);
}
static void buf_data(int bid, int target, const uint8_t* d, int n) {
    char v[65536];
    snprintf(v, sizeof(v), "%d,%d,f,35044,%s", bid, target, "");   // placeholder
    // use plain floats protocol instead
    std::string s;
    const float* f = (const float*)d;
    for (int i = 0; i < n / 4; i++) { if (i) s += ","; char b[32]; snprintf(b, sizeof(b), "%g", f[i]); s += b; }
    snprintf(v, sizeof(v), "%d,%d,%s", bid, target, s.c_str());
    gl("c0", "glbuf", v);
    char b[32]; snprintf(b, sizeof(b), "%d,%d", bid, target); gl("c0", "glbindbuf", b);
}
static void prog(int pid, int vs, int fs, int posloc, int mloc, int ploc) {
    char v[32]; snprintf(v, sizeof(v), "%d", pid); gl("c0", "glprog", v);
    char a[32]; snprintf(a, sizeof(a), "%d,%d", pid, vs); gl("c0", "glatt", a);
    snprintf(a, sizeof(a), "%d,%d", pid, fs); gl("c0", "glatt", a);
    gl("c0", "gllink", v); gl("c0", "gluse", v);
    snprintf(a, sizeof(a), "%d,%d,aPos", pid, posloc); gl("c0", "glaloc", a);
    snprintf(a, sizeof(a), "%d,%d,uM", pid, mloc); gl("c0", "gluloc", a);
    snprintf(a, sizeof(a), "%d,2,5126,16,0", posloc); gl("c0", "glattptr", a);
}
static void draw_full(int mloc) {
    float gm[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    char m[160] = "";
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", gm[i]);
    gl("c0", "gluM4", std::string("") + std::to_string(mloc) + "," + m);
    gl("c0", "draw", "4,6");
}
static const char* VS = R"GLSL(
#version 300 es
in vec2 aPos;
uniform mat4 uM;
void main() { gl_Position = uM * vec4(aPos, 0.0, 1.0); }
)GLSL";
int main() {
    gl("c0", "glctx", ""); gl("c0", "resize", "64,64");
    float quad[] = { -1,-1,0,0, 1,-1,0,0, 1,1,0,0, -1,-1,0,0, 1,1,0,0, -1,1,0,0 };
    buf_data(10, 34962, (const uint8_t*)quad, sizeof(quad));
    shader(1, 35633, VS);
    // Test 4: blend
    static const char* FS_A = R"GLSL(
#version 300 es
precision mediump float;
out vec4 o;
void main() { o = vec4(0.0, 0.0, 0.0, 0.5); }
)GLSL";
    shader(2, 35632, FS_A);
    prog(8, 1, 2, 112, 113, 0);
    gl("c0", "glc", "1,1,1,1"); gl("c0", "glclr", "16640");
    gl("c0", "enable", "3042"); gl("c0", "blend", "770,771");
    draw_full(113);
    int w=0,h=0; const uint8_t* f = frame("c0", &w, &h);
    const uint8_t* p = f + (32*w+32)*4;
    printf("blend (32,32)=%d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    // Test 5: color mask
    gl("c0", "glc", "0,1,0,1"); gl("c0", "enable", "3089"); gl("c0", "scis", "0,0,32,64"); gl("c0", "cmask", "1,1,1,1"); gl("c0", "glclr", "16640");
    f = frame("c0", &w, &h); p = f + (32*w+8)*4;
    printf("scissor (8,32)=%d,%d,%d\n", p[0], p[1], p[2]);
    gl("c0", "dis", "3089"); gl("c0", "cmask", "1,0,0,1"); gl("c0", "glc", "1,1,0,1"); gl("c0", "glclr", "16640");
    f = frame("c0", &w, &h); p = f + (32*w+40)*4;
    printf("colormask (40,32)=%d,%d,%d\n", p[0], p[1], p[2]);
    // Test 6: control flow
    static const char* FS_F = R"GLSL(
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
    gl("c0", "cmask", "1,1,1,1");
    shader(3, 35632, FS_F);
    prog(9, 1, 3, 114, 115, 0);
    gl("c0", "glc", "0,0,0,1"); gl("c0", "glclr", "16640");
    draw_full(115);
    f = frame("c0", &w, &h); p = f + (32*w+32)*4;
    printf("flow (32,32)=%d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    // piece probes
    static const char* FS_L = R"GLSL(
#version 300 es
precision mediump float;
precision mediump float;
out vec4 o;
void main() {
    float acc = 0.0;
    for (int i = 0; i < 4; i++) { acc += float(i); }
    o = vec4(acc, 0.0, 0.0, 1.0);
}
)GLSL";
    static const char* FS_S = R"GLSL(
#version 300 es
precision mediump float;
precision mediump float;
out vec4 o;
void main() {
    o = vec4(smoothstep(0.2, 0.8, 0.5), 0.0, 0.0, 1.0);
}
)GLSL";
    static const char* FS_Z = R"GLSL(
#version 300 es
precision mediump float;
precision mediump float;
out vec4 o;
void main() {
    vec3 v = vec3(1.0, 0.5, 0.25);
    o = vec4(v.zy, 0.0, 1.0);
}
)GLSL";
    shader(4, 35632, FS_L);
    prog(10, 1, 4, 116, 117, 0);
    gl("c0", "glc", "0,0,0,1"); gl("c0", "glclr", "16640");
    draw_full(117);
    f = frame("c0", &w, &h); p = f + (32*w+32)*4;
    printf("loop acc=(%d) -> %d,%d,%d,%d\n", 0, p[0], p[1], p[2], p[3]);
    shader(5, 35632, FS_S);
    prog(11, 1, 5, 118, 119, 0);
    gl("c0", "glc", "0,0,0,1"); gl("c0", "glclr", "16640");
    draw_full(119);
    f = frame("c0", &w, &h); p = f + (32*w+32)*4;
    printf("smoothstep -> %d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    shader(6, 35632, FS_Z);
    prog(12, 1, 6, 120, 121, 0);
    gl("c0", "glc", "0,0,0,1"); gl("c0", "glclr", "16640");
    draw_full(121);
    f = frame("c0", &w, &h); p = f + (32*w+32)*4;
    printf("v.zy -> %d,%d,%d,%d\n", p[0], p[1], p[2], p[3]);
    nefu::swgl_shutdown();
    return 0;
}
