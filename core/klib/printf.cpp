// klib formatted output
#include "klib.h"
#include "../platform.h"
#include <stdarg.h>

namespace nefu {

static void pchar(char*& p, char* end, char c) {
    if (p < end) *p++ = c;
}

static void pstr(char*& p, char* end, const char* s) {
    if (!s) s = "(null)";
    while (*s && p < end) *p++ = *s++;
}

static void pnum(char*& p, char* end, unsigned int v, int base, bool upper, bool sign) {
    char tmp[40];
    int n = 0;
    if (sign) {
        // for %d
    }
    do {
        int d = v % base;
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + (d - 10));
        v /= base;
    } while (v > 0 && n < 39);
    while (n > 0) pchar(p, end, tmp[--n]);
}

static void kvfmt(char* buf, size_t bufsz, const char* fmt, va_list ap) {
    char* p = buf;
    char* end = buf + bufsz - 1;
    if (bufsz == 0) return;
    for (; *fmt && p < end; fmt++) {
        if (*fmt != '%') { pchar(p, end, *fmt); continue; }
        fmt++;
        if (!*fmt) break;
        bool minus = false;
        if (*fmt == '-') { minus = true; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == 'l') fmt++; // ignore long prefix
        int pad = 0; char pc = ' ';
        if (minus) { /* 不做左对齐 */ (void)minus; }
        switch (*fmt) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int n = (int)strlen(s);
            if (width > n) { pad = width - n; }
            for (int i = 0; i < pad && p < end; i++) pchar(p, end, pc);
            pstr(p, end, s);
            break;
        }
        case 'c': pchar(p, end, (char)va_arg(ap, int)); break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { pchar(p, end, '-'); pnum(p, end, (unsigned int)(-(v + 1)) + 1, 10, false, false); }
            else pnum(p, end, (unsigned int)v, 10, false, false);
            break;
        }
        case 'u': pnum(p, end, va_arg(ap, unsigned int), 10, false, false); break;
        case 'x': pnum(p, end, va_arg(ap, unsigned int), 16, false, false); break;
        case 'X': pnum(p, end, va_arg(ap, unsigned int), 16, true, false); break;
        case 'p': pstr(p, end, "0x"); pnum(p, end, (unsigned int)(uintptr_t)va_arg(ap, void*), 16, true, false); break;
        case '%': pchar(p, end, '%'); break;
        default: pchar(p, end, '%'); pchar(p, end, *fmt); break;
        }
    }
    *p = 0;
}

int ksprintf(char* buf, size_t bufsz, const char* fmt, ...) {
    if (!buf || bufsz == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    kvfmt(buf, bufsz, fmt, ap);
    va_end(ap);
    return (int)strlen(buf);
}

void klogf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvfmt(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    platform_dbg(buf);
}

} // namespace nefu
