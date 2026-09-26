// probe: compile FS_FLOW source and report parse error
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
}
namespace nefu {
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
static void gl(const char* cmd, const char* val) { nefu::swgl_cmd("c0", cmd, val); }

int main() {
    gl("glctx", "");
    const char* variants[5] = {
        "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main() { float acc = 0.0; i; o = vec4(acc, 0.0, 0.0, 1.0); }\n",
        "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main() { float i = 1.0; i++; o = vec4(i, 0.0, 0.0, 1.0); }\n",
        "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main() { float acc = 0.0; for (;;) { acc += 1.0; if (acc > 2.0) break; } o = vec4(acc, 0.0, 0.0, 1.0); }\n",
        "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main() { float acc = 0.0; for (int i = 0; i < 4; ) { acc += 1.0; } o = vec4(acc, 0.0, 0.0, 1.0); }\n",
        "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main() { float acc = 0.0; for (int i = 0; i < 4; i++) { acc += 1.0; } o = vec4(acc, 0.0, 0.0, 1.0); }\n"
    };
    for (int k = 0; k < 5; k++) {
        char v[8192];
        snprintf(v, sizeof(v), "%d,35632,%s", k + 1, urlenc(variants[k]).c_str());
        gl("glsh", v);
    }
    printf("probe done\n");
    return 0;
}
