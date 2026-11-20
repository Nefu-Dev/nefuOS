/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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

// 64-bit unsigned print (used by %f integer part; values may exceed 32 bits
// for large unit conversions).
static void pnum64(char*& p, char* end, unsigned long long v, int base, bool upper) {
    char tmp[40];
    int n = 0;
    do {
        int d = (int)(v % (unsigned long long)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + (d - 10));
        v /= (unsigned long long)base;
    } while (v > 0 && n < 39);
    while (n > 0) pchar(p, end, tmp[--n]);
}

// Print a double with the given number of fractional digits using integer
// math only (no modf/pow), so it works identically on the host and the
// bare-metal kernel.
static void pfloat(char*& p, char* end, double v, int prec) {
    if (prec < 0) prec = 6;
    if (prec > 12) prec = 12;
    if (v < 0) { pchar(p, end, '-'); v = -v; }
    if (!(v < 1e18)) {            // overflow guard: fall back to a huge literal
        pstr(p, end, "inf");
        return;
    }
    unsigned long long ip = (unsigned long long)v;
    double fp = v - (double)ip;
    double scale = 1.0;
    for (int i = 0; i < prec; i++) scale *= 10.0;
    unsigned long long frac = (unsigned long long)(fp * scale + 0.5);
    if (frac >= (unsigned long long)(scale * 10.0)) {   // rounded up to next integer
        frac = 0;
        ip++;
    }
    pnum64(p, end, ip, 10, false);
    if (prec > 0) {
        pchar(p, end, '.');
        char tmp[16];
        for (int i = prec - 1; i >= 0; i--) { tmp[i] = (char)('0' + frac % 10); frac /= 10; }
        for (int i = 0; i < prec && p < end; i++) pchar(p, end, tmp[i]);
    }
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
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        bool is64 = false;
        if (*fmt == 'l') { fmt++; if (*fmt == 'l') { fmt++; is64 = true; } }
        int pad = 0; char pc = ' ';
        if (minus) { /* no left alignment needed */ (void)minus; }
        switch (*fmt) {
        case 'f': {
            double v = va_arg(ap, double);
            pfloat(p, end, v, prec);
            break;
        }
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
            // honour the width field ("%02d"): the old code accepted the digits
            // but never used them, so 6 printed as "6" instead of "06" - which
            // is why the clock showed "17:6" for 17:06.
            if (is64) {
                long long v = va_arg(ap, long long);
                int neg = v < 0;
                if (neg) { pchar(p, end, '-'); v = -v; }
                unsigned long long uv = (unsigned long long)v;
                int digits = 1; for (unsigned long long t = uv; t >= 10; t /= 10) digits++;
                int need = width - digits - (neg ? 1 : 0);
                for (int i = 0; i < need && p < end; i++) pchar(p, end, '0');
                pnum64(p, end, uv, 10, false);
            } else {
                int v = va_arg(ap, int);
                int neg = v < 0;
                if (neg) { pchar(p, end, '-'); v = -(v + 1) + 1; }
                unsigned int uv = (unsigned int)v;
                int digits = 1; for (unsigned int t = uv; t >= 10; t /= 10) digits++;
                int need = width - digits - (neg ? 1 : 0);
                for (int i = 0; i < need && p < end; i++) pchar(p, end, '0');
                pnum(p, end, uv, 10, false, false);
            }
            break;
        }
        case 'u': {
            unsigned int uv;
            if (is64) uv = (unsigned int)va_arg(ap, unsigned long long);
            else uv = va_arg(ap, unsigned int);
            int digits = 1; for (unsigned int t = uv; t >= 10; t /= 10) digits++;
            int need = width - digits;
            for (int i = 0; i < need && p < end; i++) pchar(p, end, '0');
            if (is64) pnum64(p, end, (unsigned long long)uv, 10, false);
            else pnum(p, end, uv, 10, false, false);
            break;
        }
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

int kvsnprintf(char* buf, size_t bufsz, const char* fmt, va_list ap) {
    if (!buf || bufsz == 0) return 0;
    kvfmt(buf, bufsz, fmt, ap);
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

