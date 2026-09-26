// probe: replicate Test1 textured quad, dump center region pixels
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
static void shader(int sid, int type, const std::string& src) {
    char v[8192];
    snprintf(v, sizeof(v), "%d,%d,%s", sid, type, urlenc(src).c_str());
    gl("c0", "glsh", v);
    char c[32];
    snprintf(c, sizeof(c), "%d", sid);
    gl("c0", "glcsh", c);
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
    gl("c0", "glbtx", b);
    snprintf(v, sizeof(v), "3553,0,32856,%d,%d,0,6408,5121,%s", w, h, b64(rgba, w * h * 4).c_str());
    gl("c0", "glteximg", v);
}

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

int main() {
    gl("c0", "glctx", "");
    gl("c0", "resize", "64,64");
    float quad[] = {
        -1,-1, 0,0,   1,-1, 1,0,   1,1, 1,1,
        -1,-1, 0,0,   1,1, 1,1,   -1,1, 0,1,
    };
    buf_data(10, 34962, (const uint8_t*)quad, sizeof(quad));
    uint8_t tex[16 * 4];
    for (int i = 0; i < 16; i++) { tex[i*4]=255; tex[i*4+1]=255; tex[i*4+2]=255; tex[i*4+3]=255; }
    tex[(2*4+2)*4+0] = 255; tex[(2*4+2)*4+1] = 0; tex[(2*4+2)*4+2] = 0;
    tex[(1*4+1)*4+0] = 0; tex[(1*4+1)*4+1] = 255; tex[(1*4+1)*4+2] = 0;
    tex_data(20, 4, 4, tex);
    gl("c0", "actex", "33984");
    shader(1, 35633, VS_QUAD);
    shader(2, 35632, FS_TEX);
    gl("c0", "glprog", "5");
    gl("c0", "glatt", "5,1");
    gl("c0", "glatt", "5,2");
    gl("c0", "gllink", "5");
    gl("c0", "gluse", "5");
    gl("c0", "glaloc", "5,101,aPos");
    gl("c0", "glaloc", "5,102,aUV");
    gl("c0", "gluloc", "5,103,uM");
    gl("c0", "gluloc", "5,104,uT");
    gl("c0", "glattptr", "101,2,5126,16,0");
    gl("c0", "glattptr", "102,2,5126,16,8");
    gl("c0", "glu1i", "104,0");
    gl("c0", "glc", "0.2,0.2,0.2,1");
    gl("c0", "glclr", "16640");
    gl("c0", "enable", "2929");
    gl("c0", "dfunc", "515");
    float gm[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    char m[160] = "";
    for (int i = 0; i < 16; i++) snprintf(m + strlen(m), sizeof(m) - strlen(m), "%s%g", i ? "," : "", gm[i]);
    gl("c0", "gluM4", std::string("103,") + m);
    gl("c0", "gltexp", "3553,10241,9728");
    gl("c0", "gltexp", "3553,10240,9728");
    gl("c0", "draw", "4,6");
    int w = 0, h = 0;
    const uint8_t* f = frame("c0", &w, &h);
    printf("w=%d h=%d\n", w, h);
    for (int y = 28; y <= 36; y++) {
        for (int x = 28; x <= 36; x++) {
            const uint8_t* p = f + ((size_t)y * w + x) * 4;
            char tag = (x == 32 && y == 32) ? '*' : ' ';
            printf("%c(%d,%d)=%d,%d,%d ", tag, x, y, p[0], p[1], p[2]);
        }
        printf("\n");
    }
    return 0;
}
