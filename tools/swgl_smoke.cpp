// Minimal swgl smoke test - prints progress to locate crash point
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
char* strrchr(const char* s, int c) { const char* last = 0; while (*s) { if (*s == (char)c) last = s; s++; } if ((char)c == 0) return (char*)s; return (char*)last; }
char* strstr(const char* h, const char* n) { if (!*n) return (char*)h; for (; *h; h++) { const char* a = h; const char* b = n; while (*b && *a == *b) { a++; b++; } if (!*b) return (char*)h; } return 0; }
int   atoi(const char* s) { int v = 0; int sg = 1; while (*s == ' ' || *s == '\t') s++; if (*s == '-') { sg = -1; s++; } else if (*s == '+') s++; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; } return v * sg; }
}
namespace nefu {
void* kalloc(size_t n) { return ::malloc(n ? n : 1); }
void  kfree(void* p) { ::free(p); }
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
static void gl(const char* cmd, const char* val) {
    nefu::swgl_cmd("c0", cmd, val);
}

int main() {
    printf("main entered\n"); fflush(stdout);
    FILE* dbg = fopen("D:/mycppos1/nefuOS/tools/swgl_dbg.txt", "w");
    #define STEP(msg) do { fputs(msg "\n", dbg); fflush(dbg); } while (0)
    printf("fopen ok: %d\n", dbg != 0); fflush(stdout);
    STEP("start");
    gl("ctx", "");
    STEP("ctx done");
    gl("resize", "64,64");
    STEP("resize done");
    const char* vs = "#version 300 es\nin vec2 aPos;\nvoid main(){ gl_Position = vec4(aPos, 0.0, 1.0); }";
    char v[8192];
    snprintf(v, sizeof(v), "1,35633,%s", urlenc(vs).c_str());
    STEP("calling glsh");
    gl("glsh", v);
    STEP("glsh done");
    gl("glcsh", "1");
    STEP("glcsh done");
    const char* fs = "#version 300 es\nprecision mediump float;\nout vec4 o;\nvoid main(){ o = vec4(1.0,0.0,0.0,1.0); }";
    char f[8192];
    snprintf(f, sizeof(f), "2,35632,%s", urlenc(fs).c_str());
    STEP("calling glsh 2");
    gl("glsh", f);
    STEP("glsh 2 done");
    gl("glcsh", "2");
    STEP("glcsh 2 done");
    gl("glprog", "5");
    gl("glatt", "5,1");
    gl("glatt", "5,2");
    STEP("linking");
    gl("gllink", "5");
    STEP("linked");
    gl("gluse", "5");
    float quad[] = { -1,-1, 1,-1, 1,1,  -1,-1, 1,1, -1,1 };
    gl("glbuf", "10,34962,f,35044,AQAAAAAAAAAAgD8AAADAPwAAAAAAAAAAgD8AAADAP4A/AAAAAAAAAACAPwAAgD8AAADAP4A/gD8AAAAA");
    // simpler: send float list
    char bf[512];
    snprintf(bf, sizeof(bf), "10,34962");
    for (int i = 0; i < 12; i++) { char t[32]; snprintf(t, sizeof(t), ",%g", quad[i]); strcat(bf, t); }
    gl("glbuf", bf);
    gl("glbindbuf", "10,34962");
    gl("glaloc", "5,101,aPos");
    gl("glattptr", "101,2,8,0");
    gl("glc", "0.2,0.2,0.2,1");
    gl("glclr", "16640");
    STEP("before draw");
    gl("draw", "4,6");
    STEP("draw done");
    int w = 0, h = 0;
    const uint8_t* fr = nefu::swgl_frame("c0", &w, &h);
    STEP("frame fetched");
    if (fr) {
        fprintf(dbg, "frame %dx%d px(1,1)=%d,%d,%d\n", w, h, fr[4], fr[5], fr[6]);
        fflush(dbg);
    }
    fclose(dbg);
    printf("ok\n");
    return 0;
}
