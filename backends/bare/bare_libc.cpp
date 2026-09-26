// nefuOS bare kernel libc shims for QuickJS.
// QuickJS (quickjs.c/libregexp.c/cutils.c/dtoa.c) and the minijs.cpp host
// driver need a small libc surface that the bare kernel does not ship:
//   malloc/free/realloc/calloc   -> nefu::kalloc/kfree/krealloc (bump arena)
//   printf family                -> self-contained vsnprintf (+ %f/%e/%g)
//   double libm (Math.*)         -> x87/SSE inline asm (entry.s enables both)
//   gettimeofday/gmtime/localtime/mktime/time -> CMOS RTC epoch math
//   abort/exit                   -> cli; hlt
// Compiled WITHOUT -mgeneral-regs-only (it needs x87/SSE codegen) alongside
// quickjs.c; unused pieces are dropped by --gc-sections at link time.
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <new>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace nefu {
void* kalloc(size_t);
void kfree(void*);
void* krealloc(void*, size_t);
}

extern "C" {

// libm forward declarations: the implementations live further down (this file
// IS the bare kernel's libm), but <math.h> cannot be included because mingw's
// version ships inline fabs/copysign that would collide with ours.
double log(double);
double fmod(double, double);

// ===================== heap =====================
void* malloc(size_t n) { return nefu::kalloc(n); }
void free(void* p) { nefu::kfree(p); }
void* realloc(void* p, size_t n) { return nefu::krealloc(p, n); }
void* calloc(size_t nmemb, size_t sz) {
    size_t total = nmemb * sz;
    void* p = nefu::kalloc(total);
    if (p) {
        unsigned char* q = (unsigned char*)p;
        for (size_t i = 0; i < total; i++) q[i] = 0;
    }
    return p;
}

// ===================== printf family =====================
// klib's ksprintf only handles %s %c %d %u %x %X %p %%; QuickJS needs
// %ld/%lld/%zu/%g etc, so this is a complete-enough printf(3) clone.

struct Fmt { char* o; char* end; };
static void fputc_(Fmt* f, char c) { if (f->o < f->end) *f->o++ = c; }
static void fputs_(Fmt* f, const char* s, int n) { while (n-- > 0 && *s) fputc_(f, *s++); }

static int fmt_u64(char* buf, uint64_t v, int base, int upper) {
    char tmp[40]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        int d = (int)(v % (uint64_t)base); v /= (uint64_t)base;
        tmp[n++] = d < 10 ? (char)('0' + d) : (char)((upper ? 'A' : 'a') + (d - 10));
    }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

// fixed (f) / scientific (e) / shortest (g) double printer for printf.
// Rounding: nearest, half-up on the last printed digit. Enough for JS
// numbers the driver serialises (WebGL args, coords, colours).
static int fmt_float(char* out, int cap, int sign, double av,
                     char conv, int prec, int* e10p) {
    int e10 = *e10p;
    if (conv == 'e' || conv == 'E') {
        // av in [1,10); digits after point
        int n = 0;
        if (sign) out[n++] = '-';
        int d0 = (int)av; av -= (double)d0;
        char dg[140];
        int nd = prec; if (nd < 0) nd = 0; if (nd > 130) nd = 130;
        for (int i = 0; i <= nd && i < 130; i++) { av *= 10.0; dg[i] = (int)av; av -= (double)dg[i]; }
        // round the (nd+1)-th digit
        if (dg[nd] >= 5) {
            for (int i = nd - 1; i >= 0; i--) {
                dg[i]++;
                if (dg[i] < 10) break;
                dg[i] = 0;
                if (i == 0) d0++;
            }
        }
        if (d0 >= 10) { d0 = 1; e10++; for (int i = 0; i < nd; i++) dg[i] = 0; }
        out[n++] = (char)('0' + d0);
        if (nd > 0) {
            out[n++] = '.';
            for (int i = 0; i < nd; i++) out[n++] = (char)('0' + dg[i]);
        }
        out[n++] = conv == 'E' ? 'E' : 'e';
        out[n++] = e10 < 0 ? '-' : '+';
        int ae = e10 < 0 ? -e10 : e10;
        out[n++] = (char)('0' + ((ae / 10) % 10));
        out[n++] = (char)('0' + (ae % 10));
        out[n] = 0;
        *e10p = e10;
        return n;
    }
    // fixed: split at the decimal point
    if (av >= 1e15) {
        // too large for exact fraction: print integer form (no fraction)
        int n = 0;
        if (sign) out[n++] = '-';
        char ib[32]; int in = 0;
        uint64_t iv = (uint64_t)av;
        if (iv == 0) ib[in++] = '0';
        while (iv) { int d = (int)(iv % 10); iv /= 10; ib[in++] = (char)('0' + d); }
        while (in) out[n++] = ib[--in];
        if (prec > 0) { out[n++] = '.'; for (int i = 0; i < prec && i < 40; i++) out[n++] = '0'; }
        out[n] = 0;
        return n;
    }
    uint64_t ip = (uint64_t)av;
    double fr = av - (double)ip;
    int nd = prec; if (nd < 0) nd = 0; if (nd > 130) nd = 130;
    char dg[140];
    for (int i = 0; i <= nd && i < 130; i++) { fr *= 10.0; dg[i] = (int)fr; fr -= (double)dg[i]; }
    if (dg[nd] >= 5) {
        for (int i = nd - 1; i >= 0; i--) {
            dg[i]++;
            if (dg[i] < 10) break;
            dg[i] = 0;
            if (i == 0) ip++;
        }
    }
    char ib[32]; int in = 0;
    if (ip == 0) ib[in++] = '0';
    while (ip) { int d = (int)(ip % 10); ip /= 10; ib[in++] = (char)('0' + d); }
    int n = 0;
    if (sign) out[n++] = '-';
    while (in) out[n++] = ib[--in];
    if (nd > 0) {
        out[n++] = '.';
        for (int i = 0; i < nd; i++) out[n++] = (char)('0' + dg[i]);
    }
    out[n] = 0;
    return n;
}

static void fmt_float_copy(Fmt* f, int sign, double av, char conv, int prec, int e10) {
    // find base-10 exponent (av kept intact here; e10 used for %g selection)
    int e = 0;
    if (av >= 1.0) { while (av >= 10.0) { av /= 10.0; e++; } }
    else if (av > 0) { while (av < 1.0) { av *= 10.0; e--; } }
    if (conv == 'g' || conv == 'G') {
        int p = prec > 0 ? prec : 1;
        if (e < -4 || e >= p) {
            char b[140]; fmt_float(b, sizeof(b), sign, av, 'e', p - 1, &e);
            // strip trailing zeros from mantissa
            int n = (int)strlen(b);
            while (n > 2 && (b[n - 1] == '0') && (b[n - 2] != 'e' && b[n - 2] != 'E')) n--;
            if (b[n - 1] == '.') n--;
            b[n] = 0;
            fputs_(f, b, n);
            return;
        }
        int frac = p - 1 - e; if (frac < 0) frac = 0;
        char b[140]; fmt_float(b, sizeof(b), sign, av, 'f', frac, &e);
        int n = (int)strlen(b);
        while (n > 0 && b[n - 1] == '0') n--;
        if (n > 0 && b[n - 1] == '.') n--;
        b[n] = 0;
        fputs_(f, b, n);
        return;
    }
    char b[140];
    int n = fmt_float(b, sizeof(b), sign, av, conv, prec, &e);
    fputs_(f, b, n);
}

int vsnprintf(char* out, size_t cap, const char* fmt, va_list ap) {
    if (cap == 0) return 0;
    Fmt f = { out, out + cap - 1 };
    const char* p = fmt;
    while (*p) {
        if (*p != '%') { fputc_(&f, *p++); continue; }
        p++;
        int left = 0, plus = 0, space = 0, zero = 0, alt = 0;
        for (;;) {
            if (*p == '-') { left = 1; p++; }
            else if (*p == '+') { plus = 1; p++; }
            else if (*p == ' ') { space = 1; p++; }
            else if (*p == '0') { zero = 1; p++; }
            else if (*p == '#') { alt = 1; p++; }
            else break;
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == '*') { width = va_arg(ap, int); p++; }
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
            if (*p == '*') { prec = va_arg(ap, int); p++; }
        }
        int ll = 0;
        if (*p == 'l') { p++; if (*p == 'l') { p++; ll = 2; } else ll = 1; }
        else if (*p == 'z' || *p == 'j') { p++; ll = 2; }
        else if (*p == 'h') { p++; if (*p == 'h') p++; }
        char conv = *p++; (void)conv;
        if (conv == '%') { fputc_(&f, '%'); continue; }
        if (conv == 'c') {
            char c = (char)va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!left && pad) { while (pad--) fputc_(&f, ' '); }
            fputc_(&f, c);
            if (left && pad) { while (pad--) fputc_(&f, ' '); }
            continue;
        }
        if (conv == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int sl = (int)strlen(s);
            if (prec >= 0 && sl > prec) sl = prec;
            int pad = width > sl ? width - sl : 0;
            if (!left && pad) { while (pad--) fputc_(&f, ' '); }
            fputs_(&f, s, sl);
            if (left && pad) { while (pad--) fputc_(&f, ' '); }
            continue;
        }
        if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
            conv == 'g' || conv == 'G') {
            double d = va_arg(ap, double);
            int sign = 0;
            if (d != d) { fputs_(&f, "nan", 3); continue; }       // NaN
            uint64_t bits; __builtin_memcpy(&bits, &d, 8);
            if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) {
                // inf
                if (bits >> 63) fputs_(&f, "-inf", 4); else fputs_(&f, "inf", 3);
                continue;
            }
            if (bits >> 63) sign = 1;
            double av = sign ? -d : d;
            int pr = prec < 0 ? 6 : prec;
            char b[140];
            int n;
            if (conv == 'g' || conv == 'G') {
                fmt_float_copy(&f, sign, av, conv, pr, 0);
                continue;
            }
            int e = 0;
            if (av >= 1.0) { while (av >= 10.0) { av /= 10.0; e++; } }
            else if (av > 0) { while (av < 1.0) { av *= 10.0; e--; } }
            n = fmt_float(b, sizeof(b), sign, av, conv, pr, &e);
            int sl = n;
            int pad = width > sl ? width - sl : 0;
            if (!left && pad) { while (pad--) fputc_(&f, zero ? '0' : ' '); }
            fputs_(&f, b, sl);
            if (left && pad) { while (pad--) fputc_(&f, ' '); }
            continue;
        }
        // integers
        uint64_t uv = 0; int is_signed = 0; int64_t iv = 0; char signch = 0;
        switch (conv) {
        case 'd': case 'i':
            is_signed = 1;
            if (ll >= 2) iv = (int64_t)va_arg(ap, long long);
            else if (ll == 1) iv = (int64_t)va_arg(ap, long);
            else iv = (int64_t)va_arg(ap, int);
            if (iv < 0) { signch = '-'; uv = (uint64_t)(-iv); }
            else { uv = (uint64_t)iv; if (plus) signch = '+'; else if (space) signch = ' '; }
            break;
        case 'u':
            if (ll >= 2) uv = va_arg(ap, unsigned long long);
            else if (ll == 1) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
            break;
        case 'x': case 'X':
            if (ll >= 2) uv = va_arg(ap, unsigned long long);
            else if (ll == 1) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
            break;
        case 'o':
            if (ll >= 2) uv = va_arg(ap, unsigned long long);
            else if (ll == 1) uv = va_arg(ap, unsigned long);
            else uv = va_arg(ap, unsigned int);
            break;
        case 'p':
            uv = (uint64_t)(uintptr_t)va_arg(ap, void*);
            conv = 'x';
            break;
        default:
            fputc_(&f, conv);
            continue;
        }
        (void)is_signed;
        char digs[64];
        int dn = 0;
        if (conv == 'x' || conv == 'X') dn = fmt_u64(digs, uv, 16, conv == 'X');
        else if (conv == 'o') dn = fmt_u64(digs, uv, 8, 0);
        else dn = fmt_u64(digs, uv, 10, 0);
        char head[8]; int hl = 0;
        if (signch) head[hl++] = signch;
        if (alt && (conv == 'x' || conv == 'X') && uv != 0) { head[hl++] = '0'; head[hl++] = conv; }
        // precision pads the digits with zeros
        if (prec > dn) {
            int need = prec - dn;
            for (int i = dn + need - 1; i >= need; i--) digs[i] = digs[i - need];
            for (int i = 0; i < need; i++) digs[i] = '0';
            dn += need;
        }
        int tlen = hl + dn;
        int pad = width > tlen ? width - tlen : 0;
        if (zero && !left && prec < 0) {
            fputs_(&f, head, hl);
            while (pad--) fputc_(&f, '0');
            fputs_(&f, digs, dn);
        } else {
            if (!left && pad) { while (pad--) fputc_(&f, ' '); }
            fputs_(&f, head, hl);
            fputs_(&f, digs, dn);
            if (left && pad) { while (pad--) fputc_(&f, ' '); }
        }
    }
    *f.o = 0;
    return (int)(f.o - out);
}

int vsprintf(char* out, const char* fmt, va_list ap) {
    return vsnprintf(out, (size_t)-1, fmt, ap);
}
int snprintf(char* out, size_t cap, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}
int sprintf(char* out, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsprintf(out, fmt, ap);
    va_end(ap);
    return r;
}

static void dbgcon(const char* s) {
    for (; *s; s++) __asm__ volatile("outb %0, %1" :: "a"((unsigned char)*s), "Nd"((unsigned short)0xE9));
}
int vprintf(const char* fmt, va_list ap) {
    char b[1024];
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    dbgcon(b);
    return n;
}
int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
int vfprintf(void* stream, const char* fmt, va_list ap) {
    (void)stream;
    return vprintf(fmt, ap);
}
int fprintf(void* stream, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}
int fputs(const char* s, void* stream) { (void)stream; dbgcon(s); return 0; }
int puts(const char* s) { dbgcon(s); dbgcon("\n"); return 0; }

// ===================== libm (double) =====================
double fabs(double x) {
    union { double d; uint64_t u; } v; v.d = x; v.u &= 0x7FFFFFFFFFFFFFFFull; return v.d;
}
static unsigned short s_round_mode(int m) {   // 0 nearest, 1 -inf, 2 +inf, 3 trunc
    unsigned short cw, ncw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    ncw = (unsigned short)((cw & 0xF3FFu) | ((unsigned)(m & 3) << 10));
    __asm__ volatile("fldcw %0" :: "m"(ncw));
    return cw;
}
static void r_round_mode(unsigned short cw) { __asm__ volatile("fldcw %0" :: "m"(cw)); }
static double x87_frndint_round(double x, int mode) {
    unsigned short cw = s_round_mode(mode);
    double r;
    __asm__ volatile("frndint" : "=t"(r) : "0"(x));
    r_round_mode(cw);
    return r;
}
double floor(double x) { return x87_frndint_round(x, 1); }
double ceil(double x) { return x87_frndint_round(x, 2); }
double trunc(double x) { return x87_frndint_round(x, 3); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double sqrt(double x) {
    double r; __asm__ volatile("fsqrt" : "=t"(r) : "0"(x)); return r;
}
double sin(double x) {
    double r; __asm__ volatile("fsin" : "=t"(r) : "0"(x)); return r;
}
double cos(double x) {
    double r; __asm__ volatile("fcos" : "=t"(r) : "0"(x)); return r;
}
double tan(double x) {
    double r; __asm__ volatile("fptan\n\tfstp %%st(1)" : "=t"(r) : "0"(x)); return r;
}
double atan(double x) {
    double r; __asm__ volatile("fld1\n\tfpatan" : "=t"(r) : "0"(x)); return r;
}
double atan2(double y, double x) {
    double r; __asm__ volatile("fpatan" : "=t"(r) : "0"(x), "u"(y)); return r;
}
double acos(double x) {
    // acos(x) = atan2(sqrt(1-x^2), x)
    double s = sqrt(1.0 - x * x);
    return atan2(s, x);
}
double asin(double x) {
    double s = sqrt(1.0 - x * x);
    return atan2(x, s);
}
double atanh(double x) {
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

// 2^x and log2(x) via x87 f2xm1 / fyl2x (QEMU TCG implements both)
static double exp2_x87(double x) {
    // 2^x = 2^frac * 2^n, frac = x - round(x) in [-0.5, 0.5] (f2xm1's domain).
    // The exponent is computed in C and pushed first so fscale picks it up as
    // st(1); this avoids fsubp, whose GAS operand form differs between builds.
    double n = x87_frndint_round(x, 0);
    double frac = x - n;
    double r;
    __asm__ volatile(
        "fldl %2\n"          // st0 = n
        "fldl %1\n"          // st0 = frac, st1 = n
        "f2xm1\n"            // st0 = 2^frac - 1
        "fld1\n"
        "faddp\n"            // st0 = 2^frac, st1 = n
        "fscale\n"           // st0 = 2^frac * 2^n
        "fstp %%st(1)\n"     // drop n, leave the result in st0
        : "=t"(r) : "m"(frac), "m"(n));
    return r;
}
static double log2_x87(double x) {
    double r;
    __asm__ volatile("fld1\n\tfyl2x" : "=t"(r) : "0"(x));
    return r;
}
double exp(double x) {
    // exp(x) = 2^(x*log2(e))
    return exp2_x87(x * 1.4426950408889634);
}
double log(double x) { return log2_x87(x) * 0.6931471805599453; }
double log10(double x) { return log2_x87(x) * 0.3010299956639812; }
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x > 0.0) return exp2_x87(y * log2_x87(x));
    if (x < 0.0) {
        double yi = trunc(y);
        if (yi == y) {
            double r = exp2_x87(y * log2_x87(-x));
            if (fmod(y, 2.0) != 0.0) r = -r;
            return r;
        }
        uint64_t b = 0x7FF8000000000000ull;   // quiet NaN
        double nan; __builtin_memcpy(&nan, &b, 8);
        return nan;
    }
    return 0.0;   // x == 0, y > 0 (y == 0 handled above)
}
double fmod(double x, double y) {
    if (y == 0.0 || x != x || y != y) {
        uint64_t b = 0x7FF8000000000000ull; double nan; __builtin_memcpy(&nan, &b, 8);
        return nan;
    }
    double r;
    __asm__ volatile(
        "1:\n\t"
        "fprem\n\t"
        "fnstsw %%ax\n\t"
        "testb $0x04, %%ah\n\t"
        "jnz 1b\n\t"
        : "=t"(r) : "0"(x), "u"(y) : "ax", "cc");
    return r;
}
double fmin(double a, double b) {
    if (a != a) return b;
    if (b != b) return a;
    return a < b ? a : b;
}
double fmax(double a, double b) {
    if (a != a) return b;
    if (b != b) return a;
    return a > b ? a : b;
}
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double modf(double x, double* ip) {
    if (x != x) { *ip = x; return x; }
    if (x == 0.0) { *ip = x; return x; }   // preserves -0
    double i = trunc(x);
    *ip = i;
    return x - i;
}
double copysign(double x, double y) {
    union { double d; uint64_t u; } v; v.d = x;
    union { double d; uint64_t u; } s; s.d = y;
    v.u = (v.u & 0x7FFFFFFFFFFFFFFFull) | (s.u & 0x8000000000000000ull);
    return v.d;
}

// ===================== time (CMOS RTC) =====================
static unsigned char cmos_read(unsigned char reg) {
    __asm__ volatile("outb %0, %1" :: "a"(reg), "Nd"((unsigned short)0x70));
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"((unsigned short)0x71));
    return v;
}
static int bcd2(int v) { return (v & 0xF) + ((v >> 4) * 10); }

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

static int64_t rtc_epoch(void) {
    while (cmos_read(0x0A) & 0x80) { }   // wait for UIP clear
    int sec = bcd2(cmos_read(0x00));
    int min = bcd2(cmos_read(0x02));
    int hour = bcd2(cmos_read(0x04));
    int day = bcd2(cmos_read(0x07));
    int mon = bcd2(cmos_read(0x08));
    int year = bcd2(cmos_read(0x09)) + 2000;
    if (year < 2000) year += 100;        // QEMU may leave year < 2000 (older RTC)
    int64_t days = days_from_civil(year, mon, day);
    return days * 86400 + hour * 3600 + min * 60 + sec;
}

int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    int64_t s = rtc_epoch();
    if (tv) { tv->tv_sec = (long)s; tv->tv_usec = 0; }
    return 0;
}

time_t time(time_t* t) {
    time_t s = (time_t)rtc_epoch();
    if (t) *t = s;
    return s;
}

static struct tm s_tm;
struct tm* gmtime(const time_t* t) {
    int64_t secs = (int64_t)*t;
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int y; unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    int hh = (int)(rem / 3600);
    int mm = (int)((rem % 3600) / 60);
    int ss = (int)(rem % 60);
    // day-of-week: days_from_civil(1970,1,1) = 0, 1970-01-01 was Thursday (4)
    int wd = (int)((days + 4) % 7); if (wd < 0) wd += 7;
    int yd = (int)(days - days_from_civil(y, 1, 1));
    s_tm.tm_sec = ss;
    s_tm.tm_min = mm;
    s_tm.tm_hour = hh;
    s_tm.tm_mday = (int)d;
    s_tm.tm_mon = (int)m - 1;
    s_tm.tm_year = y - 1900;
    s_tm.tm_wday = wd;
    s_tm.tm_yday = yd;
    s_tm.tm_isdst = 0;
    return &s_tm;
}
struct tm* localtime(const time_t* t) { return gmtime(t); }   // UTC, offset 0
time_t mktime(struct tm* tm) {
    int64_t days = days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}

// ===================== abort / exit =====================
static void power_halt(void) {
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
void abort(void) { power_halt(); }
void exit(int code) { (void)code; power_halt(); }
void _exit(int code) { (void)code; power_halt(); }
int __cdecl _purecall(void) { power_halt(); return 0; }

} // extern "C"

// ===================== C++ STL runtime stubs =====================
// Kernel apps use std::string/vector/map; the bare kernel links no
// libstdc++.so, so provide the handful of symbols those headers reference:
// operator new/delete (-> heap above) and the std::__throw_* abort traps
// (exceptions are disabled, but templates still call them on errors).
// Compiled only for the bare build (host build links the real CRT).
static void stl_abort(void) __attribute__((noreturn));
static void stl_abort(void) {
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}


void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }

namespace std {
#define NEFU_THROW_STUB(name) \
    void name() __attribute__((noreturn)); \
    void name() { stl_abort(); }
NEFU_THROW_STUB(__throw_bad_alloc)
NEFU_THROW_STUB(__throw_bad_array_new_length)
#undef NEFU_THROW_STUB
#define NEFU_THROW_STUB1(name) \
    void name(const char*) __attribute__((noreturn)); \
    void name(const char*) { stl_abort(); }
NEFU_THROW_STUB1(__throw_length_error)
NEFU_THROW_STUB1(__throw_logic_error)
NEFU_THROW_STUB1(__throw_out_of_range)
NEFU_THROW_STUB1(__throw_runtime_error)
NEFU_THROW_STUB1(__throw_domain_error)
NEFU_THROW_STUB1(__throw_invalid_argument)
NEFU_THROW_STUB1(__throw_overflow_error)
NEFU_THROW_STUB1(__throw_underflow_error)
#undef NEFU_THROW_STUB1
} // namespace std

extern "C" void __cxa_pure_virtual() { stl_abort(); }

// ============================================================================
// Extended runtime surface: the whole core tree (math/audio/gfx/dblib/sim/...)
// is compiled into the freestanding kernel with the standard SysV ABI (x87/SSE
// available; entry.s enables them at boot).  This block fills the remaining
// libc + libstdc++ runtime holes so everything links:
//   memchr / ctype / rand / atexit / strtod / atof
//   __mingw_* stdio aliases (MinGW headers remap printf/snprintf/strtod)
//   CRT import thunks (__acrt_iob_func, _msize, isalnum/isspace)
//   missing libm: cosh/sinh/tanh/acosh/asinh/expm1/log1p/log2/cbrt/lrint
//   float wrappers for the double libm
//   128-bit integer division helpers (__udivti3/__modti3/__udivmodti4)
//   C++ runtime: __cxa_guard_*, std::_Rb_tree_*, __throw_out_of_range_fmt
// ============================================================================

extern "C" {

// ---------- memchr ----------
void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char t = (unsigned char)c;
    while (n--) { if (*p == t) return (void*)p; p++; }
    return 0;
}

// ---------- ctype (implemented locally; <ctype.h> macros would remap to
//            __imp_* CRT imports and fight the kernel) ----------
static int ct_isalnum(int c)  { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int ct_isalpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int ct_isdigit(int c)  { return c >= '0' && c <= '9'; }
static int ct_isxdigit(int c) { return ct_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int ct_isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int ct_isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static int ct_islower(int c)  { return c >= 'a' && c <= 'z'; }
static int ct_ispunct(int c)  { return (c >= 33 && c <= 126) && !ct_isalnum(c); }
static int ct_isgraph(int c)  { return c >= 33 && c <= 126; }
static int ct_isprint(int c)  { return c >= 32 && c <= 126; }
static int ct_iscntrl(int c)  { return (c >= 0 && c <= 31) || c == 127; }
static int ct_isblank(int c)  { return c == ' ' || c == '\t'; }

int isalnum(int c)  { return ct_isalnum(c); }
int isalpha(int c)  { return ct_isalpha(c); }
int isdigit(int c)  { return ct_isdigit(c); }
int isxdigit(int c) { return ct_isxdigit(c); }
int isspace(int c)  { return ct_isspace(c); }
int isupper(int c)  { return ct_isupper(c); }
int islower(int c)  { return ct_islower(c); }
int ispunct(int c)  { return ct_ispunct(c); }
int isgraph(int c)  { return ct_isgraph(c); }
int isprint(int c)  { return ct_isprint(c); }
int iscntrl(int c)  { return ct_iscntrl(c); }
int isblank(int c)  { return ct_isblank(c); }
int tolower(int c)  { return ct_isupper(c) ? c + 32 : c; }
int toupper(int c)  { return ct_islower(c) ? c - 32 : c; }
// MinGW headers reach these through CRT import thunks (call *__imp_x(%rip)).
void* __imp_isalnum = (void*)&isalnum;
void* __imp_isspace = (void*)&isspace;
void* __imp_isalpha = (void*)&isalpha;

// ---------- rand / srand ----------
static unsigned long s_rand_state = 0x12345678ul;
int rand(void) {
    s_rand_state = s_rand_state * 1103515245ul + 12345ul;
    return (int)((s_rand_state >> 16) & 0x7FFF);
}
void srand(unsigned int seed) { s_rand_state = seed ? seed : 1u; }

// ---------- atexit (kernel never unwinds; accept and ignore) ----------
int atexit(void (*fn)(void)) { (void)fn; return 0; }

// ---------- strtod / atof ----------
static double dbl_inf(void) { double x = 1e308; return x * 10.0; }   // overflows to +inf
static double dbl_nan(void) { return dbl_inf() - dbl_inf(); }

double strtod(const char* s, char** end) {
    const char* p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    // inf / infinity
    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F')) {
        p += 3;
        if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') &&
            (p[2] == 'i' || p[2] == 'I') && (p[3] == 't' || p[3] == 'T') && (p[4] == 'y' || p[4] == 'Y')) p += 5;
        if (end) *end = (char*)p;
        double v = dbl_inf();
        return neg ? -v : v;
    }
    // nan / nan(chars)
    if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A')) {
        const char* q = p + 2;
        if (q[0] == 'n' || q[0] == 'N') { p = q + 1; if (end) *end = (char*)p; return dbl_nan(); }
        if (q[0] == '(') { while (*q && *q != ')') q++; if (*q == ')') q++; }
        if (end) *end = (char*)q;
        return dbl_nan();
    }
    // hex float: 0x1.8p3
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (ct_isxdigit((unsigned char)p[2]) || p[2] == '.')) {
        const char* q = p + 2;
        double m = 0.0;
        int saw = 0, exp2 = 0;
        while (ct_isxdigit((unsigned char)*q)) {
            int d = *q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10;
            m = m * 16.0 + (double)d; q++; saw = 1;
        }
        if (*q == '.') {
            q++;
            while (ct_isxdigit((unsigned char)*q)) {
                int d = *q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10;
                m = m * 16.0 + (double)d; exp2 -= 4; q++; saw = 1;
            }
        }
        if (!saw) { if (end) *end = (char*)p; return 0.0; }
        if (*q == 'p' || *q == 'P') {
            q++;
            int eneg = 0;
            if (*q == '+' || *q == '-') { eneg = (*q == '-'); q++; }
            int e = 0;
            while (*q >= '0' && *q <= '9') { e = e * 10 + (*q - '0'); if (e > 5000) e = 5000; q++; }
            if (eneg) e = -e;
            exp2 += e;
        }
        double r = m;
        if (exp2 > 0) r *= exp2_x87((double)exp2);
        else if (exp2 < 0) r *= exp2_x87((double)exp2);
        if (end) *end = (char*)q;
        return neg ? -r : r;
    }
    // decimal
    double m = 0.0;
    int ndig = 0, frac = 0;
    while (*p >= '0' && *p <= '9') { m = m * 10.0 + (*p - '0'); p++; ndig++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { m = m * 10.0 + (*p - '0'); p++; frac++; ndig++; }
    }
    if (ndig == 0) { if (end) *end = (char*)s; return 0.0; }
    int e10 = -frac;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        int eneg = 0;
        if (*q == '+' || *q == '-') { eneg = (*q == '-'); q++; }
        int ev = 0, any = 0;
        while (*q >= '0' && *q <= '9') { ev = ev * 10 + (*q - '0'); if (ev > 10000) ev = 10000; q++; any = 1; }
        if (any) { if (eneg) ev = -ev; e10 += ev; p = q; }
    }
    double r = m;
    if (e10 > 0) r *= exp2_x87(log2_x87(10.0) * (double)e10);
    else if (e10 < 0) r *= exp2_x87(log2_x87(10.0) * (double)e10);
    if (end) *end = (char*)p;
    return neg ? -r : r;
}

double atof(const char* s) { return strtod(s, 0); }

// ---------- MinGW stdio name remaps (headers compile printf->__mingw_printf
//            etc.; forward to the kernel implementations above) ----------
int __mingw_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vprintf(fmt, ap); va_end(ap); return r;
}
int __mingw_fprintf(void* stream, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vfprintf(stream, fmt, ap); va_end(ap); return r;
}
int __mingw_sprintf(char* out, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsprintf(out, fmt, ap); va_end(ap); return r;
}
int __mingw_snprintf(char* out, size_t cap, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(out, cap, fmt, ap); va_end(ap); return r;
}
int __mingw_vsnprintf(char* out, size_t cap, const char* fmt, va_list ap) {
    return vsnprintf(out, cap, fmt, ap);
}
double __mingw_strtod(const char* s, char** end) { return strtod(s, end); }

// ---------- CRT import thunks ----------
void* __acrt_iob_func(unsigned idx) { (void)idx; static char dummy[64]; return dummy; }
void* __imp___acrt_iob_func = (void*)&__acrt_iob_func;
size_t _msize(void* p) { (void)p; return 0; }
void* __imp__msize = (void*)&_msize;

// ---------- missing libm (x87; helpers live in this file) ----------
double cosh(double x) { double e = exp(x); return 0.5 * (e + 1.0 / e); }
double sinh(double x) { double e = exp(x); return 0.5 * (e - 1.0 / e); }
double tanh(double x) {
    if (x != x) return x;
    if (x >= 20.0) return 1.0;
    if (x <= -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
double acosh(double x) { return log(x + sqrt(x * x - 1.0)); }
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double expm1(double x) {
    double ax = x < 0.0 ? -x : x;
    if (ax < 0.01) return x * (1.0 + x * (0.5 + x * (1.0 / 6.0 + x * (1.0 / 24.0 + x / 120.0))));
    return exp(x) - 1.0;
}
double log1p(double x) {
    double ax = x < 0.0 ? -x : x;
    if (ax < 0.01) return x * (1.0 - x * (0.5 - x * (1.0 / 3.0 - x * (0.25 - x / 5.0))));
    return log(1.0 + x);
}
double cbrt(double x) {
    if (x == 0.0) return x;
    if (x < 0.0) return -exp(log(-x) / 3.0);
    return exp(log(x) / 3.0);
}
double log2(double x) { return log2_x87(x); }
long lrint(double x) { return (long)x87_frndint_round(x, 0); }

// ---------- float wrappers (SVG / audio / gfx paths use the float API) ----------
float sinf(float x)   { return (float)sin((double)x); }
float cosf(float x)   { return (float)cos((double)x); }
float tanf(float x)   { return (float)tan((double)x); }
float sqrtf(float x)  { return (float)sqrt((double)x); }
float fabsf(float x)  { return (float)fabs((double)x); }
float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x)  { return (float)ceil((double)x); }
float roundf(float x) { return (float)round((double)x); }
float truncf(float x) { return (float)trunc((double)x); }
float atanf(float x)  { return (float)atan((double)x); }
float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
float asinf(float x)  { return (float)asin((double)x); }
float acosf(float x)  { return (float)acos((double)x); }
float powf(float a, float b) { return (float)pow((double)a, (double)b); }
float expf(float x)   { return (float)exp((double)x); }
float exp2f(float x)  { return (float)exp2_x87((double)x); }
float expm1f(float x) { return (float)expm1((double)x); }
float logf(float x)   { return (float)log((double)x); }
float log2f(float x)  { return (float)log2((double)x); }
float log10f(float x) { return (float)log10((double)x); }
float log1pf(float x) { return (float)log1p((double)x); }
float fmodf(float a, float b) { return (float)fmod((double)a, (double)b); }
float fminf(float a, float b) { return (float)fmin((double)a, (double)b); }
float fmaxf(float a, float b) { return (float)fmax((double)a, (double)b); }
float copysignf(float a, float b) { return (float)copysign((double)a, (double)b); }
float hypotf(float a, float b) { return (float)hypot((double)a, (double)b); }
float sinhf(float x)  { return (float)sinh((double)x); }
float coshf(float x)  { return (float)cosh((double)x); }
float tanhf(float x)  { return (float)tanh((double)x); }
float asinhf(float x) { return (float)asinh((double)x); }
float acoshf(float x) { return (float)acosh((double)x); }
float atanhf(float x) { return (float)atanh((double)x); }
float cbrtf(float x)  { return (float)cbrt((double)x); }
float modff(float x, float* ip) { double d; double f = modf((double)x, &d); *ip = (float)d; return (float)f; }

// ---------- sincosf (GCC -O2 folds sinf(x)+cosf(x) into a sincosf call) ----------
void sincosf(float x, float* s, float* c) {
    *s = (float)sin((double)x);
    *c = (float)cos((double)x);
}

// ---------- strtol / strtoll (nanosvg & misc parse numbers) ----------
static unsigned long long strtoull_core(const char* nptr, char** endptr, int base) {
    const char* p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f') p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    unsigned long long acc = 0;
    int any = 0;
    int b = base;
    if (b == 0) {
        if (*p == '0') { p++; b = (*p == 'x' || *p == 'X') ? (p++, 16) : 8; }
        else b = 10;
    } else if (b == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; }
    const unsigned long long cutoff = (unsigned long long)-1 / 2;  // saturate
    for (;;) {
        int c = (unsigned char)*p;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= b) break;
        if (acc > (cutoff - (unsigned long long)d) / (unsigned long long)b) acc = cutoff;
        else acc = acc * (unsigned long long)b + (unsigned long long)d;
        any = 1;
        p++;
    }
    if (endptr) *endptr = (char*)(any ? p : nptr);
    return neg ? (unsigned long long)-(long long)acc : acc;
}
long strtol(const char* nptr, char** endptr, int base) {
    unsigned long long v = strtoull_core(nptr, endptr, base);
    return (long)((unsigned long)v);
}
long long strtoll(const char* nptr, char** endptr, int base) {
    unsigned long long v = strtoull_core(nptr, endptr, base);
    return (long long)v;
}

// ---------- qsort (nanosvg sorts its small SVG element arrays) ----------
void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*)) {
    if (!base || nmemb < 2 || size == 0) return;
    char* b = (char*)base;
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0; j--) {
            if (cmp(b + (j - 1) * size, b + j * size) <= 0) break;
            for (size_t k = 0; k < size; k++) {
                char t = b[(j - 1) * size + k];
                b[(j - 1) * size + k] = b[j * size + k];
                b[j * size + k] = t;
            }
        }
    }
}

// ---------- _assert (MinGW assert() remaps to this) ----------
void _assert(const char* expr, const char* file, int line) {
    __mingw_printf("assert(%s) failed at %s:%d\n", expr, file, line);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

// ---------- __mingw_sscanf (MinGW headers remap sscanf -> this) ----------
int __mingw_sscanf(const char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int count = 0;
    const char* p = s;
    while (*fmt) {
        if (*fmt != '%') { fmt++; continue; }
        fmt++;
        if (*fmt == '%') { fmt++; continue; }               // literal %
        int suppress = 0;
        while (*fmt == '*') { suppress = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        else if (*fmt == 'h') { fmt++; }
        char conv = *fmt;
        if (!conv) break;
        fmt++;
        if (conv != 'c' && conv != 'n') {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        }
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'o') {
            int b = conv == 'x' ? 16 : conv == 'o' ? 8 : conv == 'i' ? 0 : 10;
            long long v = strtoll(p, (char**)&p, b);
            if (!suppress) {
                if (is_long) {
                    if (conv == 'u') *(unsigned long*)va_arg(ap, unsigned long*) = (unsigned long)v;
                    else *(long*)va_arg(ap, long*) = (long)v;
                } else {
                    *(int*)va_arg(ap, int*) = (int)v;
                }
            }
            count++;
        } else if (conv == 'f' || conv == 'g' || conv == 'e') {
            double v = strtod(p, (char**)&p);
            if (!suppress) {
                if (is_long) *(double*)va_arg(ap, double*) = v;
                else *(float*)va_arg(ap, float*) = (float)v;
            }
            count++;
        } else if (conv == 's') {
            const char* start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            if (!suppress) {
                char* out = va_arg(ap, char*);
                int n = (int)(p - start);
                int lim = width > 0 && width < n ? width : n;
                for (int i = 0; i < lim; i++) out[i] = start[i];
                out[lim] = 0;
            }
            count++;
        } else if (conv == 'c') {
            if (*p) {
                if (!suppress) {
                    char* out = va_arg(ap, char*);
                    *out = *p;
                }
                p++;
                count++;
            }
        } else if (conv == 'n') {
            if (!suppress) *(int*)va_arg(ap, int*) = (int)(p - s);
        }
    }
    va_end(ap);
    return count;
}

// ---------- 128-bit integer helpers (QuickJS bignum + numtheory) ----------
typedef unsigned __int128 nefu_u128;
typedef __int128 nefu_i128;

nefu_u128 __udivmodti4(nefu_u128 n, nefu_u128 d, nefu_u128* rem) {
    if (d == 0) { if (rem) *rem = n; return 0; }
    nefu_u128 q = 0, r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | (nefu_u128)((n >> i) & 1);
        if (r >= d) { r -= d; q |= ((nefu_u128)1 << i); }
    }
    if (rem) *rem = r;
    return q;
}
nefu_u128 __udivti3(nefu_u128 a, nefu_u128 b) { return __udivmodti4(a, b, 0); }
nefu_u128 __umodti3(nefu_u128 a, nefu_u128 b) { nefu_u128 r; __udivmodti4(a, b, &r); return r; }
nefu_i128 __modti3(nefu_i128 a, nefu_i128 b) {
    nefu_u128 ua = a < 0 ? (nefu_u128)(-a) : (nefu_u128)a;
    nefu_u128 ub = b < 0 ? (nefu_u128)(-b) : (nefu_u128)b;
    nefu_u128 r;
    __udivmodti4(ua, ub, &r);
    return a < 0 ? -(nefu_i128)r : (nefu_i128)r;
}

} // extern "C"

// ---------- C++ runtime: __cxa_guard (single-threaded kernel) ----------
extern "C" int __cxa_guard_acquire(unsigned long long* g) {
    unsigned char* b = (unsigned char*)g;
    if (*b == 2) return 0;                       // already initialized
    if (*b == 1) { while (*b == 1) { } return *b != 2; }  // re-entrant spin
    *b = 1;                                       // init in progress
    return 1;
}
extern "C" void __cxa_guard_release(unsigned long long* g) { *(unsigned char*)g = 2; }
extern "C" void __cxa_guard_abort(unsigned long long* g) { *(unsigned char*)g = 0; }

// ---------- C++ runtime: std::_Rb_tree_* (libstdc++ runtime functions that
//            std::map/set headers call; layout must match stl_tree.h) ----------
namespace std {

struct _Rb_tree_node_base {
    _Rb_tree_node_base* _M_parent;
    _Rb_tree_node_base* _M_left;
    _Rb_tree_node_base* _M_right;
    int _M_color;   // 0 = red, 1 = black (same layout as libstdc++ enum)
};

static _Rb_tree_node_base* rb_min(_Rb_tree_node_base* x) {
    while (x->_M_left) x = x->_M_left;
    return x;
}
static void rb_rotate_left(_Rb_tree_node_base* x, _Rb_tree_node_base*& root) {
    _Rb_tree_node_base* y = x->_M_right;
    x->_M_right = y->_M_left;
    if (y->_M_left) y->_M_left->_M_parent = x;
    y->_M_parent = x->_M_parent;
    if (x == root) root = y;
    else if (x == x->_M_parent->_M_left) x->_M_parent->_M_left = y;
    else x->_M_parent->_M_right = y;
    y->_M_left = x;
    x->_M_parent = y;
}
static void rb_rotate_right(_Rb_tree_node_base* x, _Rb_tree_node_base*& root) {
    _Rb_tree_node_base* y = x->_M_left;
    x->_M_left = y->_M_right;
    if (y->_M_right) y->_M_right->_M_parent = x;
    y->_M_parent = x->_M_parent;
    if (x == root) root = y;
    else if (x == x->_M_parent->_M_right) x->_M_parent->_M_right = y;
    else x->_M_parent->_M_left = y;
    y->_M_right = x;
    x->_M_parent = y;
}

_Rb_tree_node_base* _Rb_tree_increment(_Rb_tree_node_base* x) {
    if (x->_M_right) return rb_min(x->_M_right);
    _Rb_tree_node_base* y = x->_M_parent;
    while (x == y->_M_right) { x = y; y = y->_M_parent; }
    return y;
}
_Rb_tree_node_base* _Rb_tree_decrement(_Rb_tree_node_base* x) {
    if (x->_M_color == 1 && x->_M_parent->_M_parent == x) return x->_M_right;  // header
    if (x->_M_left) {
        _Rb_tree_node_base* y = x->_M_left;
        while (y->_M_right) y = y->_M_right;
        return y;
    }
    _Rb_tree_node_base* y = x->_M_parent;
    while (x == y->_M_left) { x = y; y = y->_M_parent; }
    return y;
}

void _Rb_tree_insert_and_rebalance(bool insert_left, _Rb_tree_node_base* x,
                                   _Rb_tree_node_base* p, _Rb_tree_node_base& header) {
    _Rb_tree_node_base*& root = header._M_parent;
    x->_M_parent = p;
    x->_M_left = 0;
    x->_M_right = 0;
    x->_M_color = 0;
    if (insert_left) {
        p->_M_left = x;
        if (p == &header) { header._M_parent = x; header._M_right = x; }
        else if (p == header._M_left) header._M_left = x;
    } else {
        p->_M_right = x;
        if (p == header._M_right) header._M_right = x;
    }
    while (x != root && x->_M_parent->_M_color == 0) {
        _Rb_tree_node_base* xpp = x->_M_parent->_M_parent;
        if (x->_M_parent == xpp->_M_left) {
            _Rb_tree_node_base* y = xpp->_M_right;
            if (y && y->_M_color == 0) {
                x->_M_parent->_M_color = 1;
                y->_M_color = 1;
                xpp->_M_color = 0;
                x = xpp;
            } else {
                if (x == x->_M_parent->_M_right) { x = x->_M_parent; rb_rotate_left(x, root); }
                x->_M_parent->_M_color = 1;
                xpp->_M_color = 0;
                rb_rotate_right(xpp, root);
            }
        } else {
            _Rb_tree_node_base* y = xpp->_M_left;
            if (y && y->_M_color == 0) {
                x->_M_parent->_M_color = 1;
                y->_M_color = 1;
                xpp->_M_color = 0;
                x = xpp;
            } else {
                if (x == x->_M_parent->_M_left) { x = x->_M_parent; rb_rotate_right(x, root); }
                x->_M_parent->_M_color = 1;
                xpp->_M_color = 0;
                rb_rotate_left(xpp, root);
            }
        }
    }
    root->_M_color = 1;
}

_Rb_tree_node_base* _Rb_tree_rebalance_for_erase(_Rb_tree_node_base* z,
                                                 _Rb_tree_node_base& header) {
    _Rb_tree_node_base*& root = header._M_parent;
    _Rb_tree_node_base* y = z;
    _Rb_tree_node_base* x = 0;
    _Rb_tree_node_base* x_parent = 0;
    if (y->_M_left == 0) x = y->_M_right;
    else if (y->_M_right == 0) x = y->_M_left;
    else {
        y = y->_M_right;
        while (y->_M_left) y = y->_M_left;
        x = y->_M_right;
    }
    if (y != z) {
        z->_M_left->_M_parent = y;
        y->_M_left = z->_M_left;
        if (y != z->_M_right) {
            x_parent = y->_M_parent;
            if (x) x->_M_parent = y->_M_parent;
            y->_M_parent->_M_left = x;
            y->_M_right = z->_M_right;
            z->_M_right->_M_parent = y;
        } else x_parent = y;
        if (root == z) root = y;
        else if (z->_M_parent->_M_left == z) z->_M_parent->_M_left = y;
        else z->_M_parent->_M_right = y;
        y->_M_parent = z->_M_parent;
        int c = y->_M_color; y->_M_color = z->_M_color; z->_M_color = c;
        y = z;
    } else {
        x_parent = y->_M_parent;
        if (x) x->_M_parent = y->_M_parent;
        if (root == z) root = x;
        else if (z->_M_parent->_M_left == z) z->_M_parent->_M_left = x;
        else z->_M_parent->_M_right = x;
        if (header._M_left == z) header._M_left = x;
        if (header._M_right == z) header._M_right = x;
    }
    if (y->_M_color != 1) {
        while (x != root && (x == 0 || x->_M_color == 1)) {
            if (x == x_parent->_M_left) {
                _Rb_tree_node_base* w = x_parent->_M_right;
                if (w->_M_color == 0) {
                    w->_M_color = 1;
                    x_parent->_M_color = 0;
                    rb_rotate_left(x_parent, root);
                    w = x_parent->_M_right;
                }
                if ((w->_M_left == 0 || w->_M_left->_M_color == 1) &&
                    (w->_M_right == 0 || w->_M_right->_M_color == 1)) {
                    w->_M_color = 0;
                    x = x_parent;
                    x_parent = x_parent->_M_parent;
                } else {
                    if (w->_M_right == 0 || w->_M_right->_M_color == 1) {
                        w->_M_left->_M_color = 1;
                        w->_M_color = 0;
                        rb_rotate_right(w, root);
                        w = x_parent->_M_right;
                    }
                    w->_M_color = x_parent->_M_color;
                    x_parent->_M_color = 1;
                    if (w->_M_right) w->_M_right->_M_color = 1;
                    rb_rotate_left(x_parent, root);
                    break;
                }
            } else {
                _Rb_tree_node_base* w = x_parent->_M_left;
                if (w->_M_color == 0) {
                    w->_M_color = 1;
                    x_parent->_M_color = 0;
                    rb_rotate_right(x_parent, root);
                    w = x_parent->_M_left;
                }
                if ((w->_M_right == 0 || w->_M_right->_M_color == 1) &&
                    (w->_M_left == 0 || w->_M_left->_M_color == 1)) {
                    w->_M_color = 0;
                    x = x_parent;
                    x_parent = x_parent->_M_parent;
                } else {
                    if (w->_M_left == 0 || w->_M_left->_M_color == 1) {
                        w->_M_right->_M_color = 1;
                        w->_M_color = 0;
                        rb_rotate_left(w, root);
                        w = x_parent->_M_left;
                    }
                    w->_M_color = x_parent->_M_color;
                    x_parent->_M_color = 1;
                    if (w->_M_left) w->_M_left->_M_color = 1;
                    rb_rotate_right(x_parent, root);
                    break;
                }
            }
        }
        if (x) x->_M_color = 1;
    }
    return y;
}

void __throw_out_of_range_fmt(const char*, ...) __attribute__((noreturn));
void __throw_out_of_range_fmt(const char*, ...) { stl_abort(); }

} // namespace std

// ============================================================================
// std::__cxx11::basic_string out-of-line members (libstdc++ ABI, GCC 15)
// ----------------------------------------------------------------------------
// dblib/simx/mathx/gfx instantiate basic_string operations that libstdc++
// emits out-of-line.  The bare kernel links no libstdc++.so, so we provide
// exactly the symbols the linker asked for, replicating the semantics of
// basic_string.tcc against libstdc++'s C++11 layout:
//
//     +0  _M_p                     (char*, data or &_M_local_buf)
//     +8  _M_string_length         (size_type)
//     +16 union { _M_local_buf[16]  |  _M_allocated_capacity }
//
// SSO: _M_p == this+16, capacity() == 15 (one byte reserved for the NUL).
// Heap: capacity stored in the union.  All growth goes through _M_create with
// libstdc++'s exponential policy.  These definitions are only instantiated
// for char; the callers were compiled against the real <string> header.
// ============================================================================

namespace std {
template<class _Tp> struct char_traits;
template<class _Tp> class allocator;
namespace __cxx11 {

template<class _CharT, class _Traits, class _Alloc>
class basic_string {
public:
    typedef unsigned long long size_type;
    basic_string() {}
    size_type find(_CharT c, size_type pos) const;
    void _M_dispose();
    void _M_replace(size_type pos, size_type len1, const _CharT* s, size_type len2);
    void _M_append(const _CharT* s, size_type n);
    void _M_assign(const basic_string& other);
    _CharT* _M_create(size_type& cap, size_type old_cap);
    void _M_mutate(size_type pos, size_type len1, const _CharT* s, size_type len2);
};

namespace {
struct StrLayout {
    char* p;
    unsigned long long len;
    union { char local[16]; unsigned long long cap; };
};
inline StrLayout* SL(void* s) { return (StrLayout*)s; }
inline unsigned long long str_len(void* s) { return SL(s)->len; }
inline char* str_local(void* s) { return (char*)s + 16; }
inline bool str_is_local(void* s) { return SL(s)->p == str_local(s); }
inline unsigned long long str_cap(void* s) {
    return str_is_local(s) ? 15 : SL(s)->cap;
}
inline void str_set_length(void* s, unsigned long long n) {
    SL(s)->len = n;
    SL(s)->p[n] = 0;
}
inline bool str_disjunct(void* s, const char* sp) {
    if (!sp) return true;
    unsigned long long a = (unsigned long long)sp;
    unsigned long long d = (unsigned long long)SL(s)->p;
    return a < d || (d + SL(s)->len) <= a;
}
inline char* str_alloc(unsigned long long n) {
    char* p = (char*)malloc(n ? n : 1);
    if (!p) stl_abort();
    return p;
}
} // namespace

template<class _CharT, class _Traits, class _Alloc>
_CharT* basic_string<_CharT,_Traits,_Alloc>::_M_create(size_type& cap, size_type old_cap) {
    // exponential growth policy (basic_string.tcc lines 148-165)
    if (cap > old_cap && cap < 2 * old_cap) cap = 2 * old_cap;
    return (_CharT*)str_alloc((cap + 1) * sizeof(_CharT));
}

template<class _CharT, class _Traits, class _Alloc>
void basic_string<_CharT,_Traits,_Alloc>::_M_dispose() {
    if (!str_is_local(this)) free(SL(this)->p);
}

template<class _CharT, class _Traits, class _Alloc>
void basic_string<_CharT,_Traits,_Alloc>::_M_mutate(size_type pos, size_type len1, const _CharT* s, size_type len2) {
    const size_type how_much = str_len(this) - pos - len1;
    size_type new_cap = str_len(this) + len2 - len1;
    _CharT* r = _M_create(new_cap, str_cap(this));
    if (pos) memcpy(r, SL(this)->p, pos);
    if (s && len2) memcpy(r + pos, s, len2);
    if (how_much) memcpy(r + pos + len2, SL(this)->p + pos + len1, how_much);
    _M_dispose();
    SL(this)->p = (char*)r;
    SL(this)->cap = new_cap;
}

template<class _CharT, class _Traits, class _Alloc>
void basic_string<_CharT,_Traits,_Alloc>::_M_append(const _CharT* s, size_type n) {
    const size_type len = n + str_len(this);
    if (len <= str_cap(this)) {
        if (n) memcpy(SL(this)->p + str_len(this), s, n);
    } else {
        _M_mutate(str_len(this), 0, s, n);
    }
    str_set_length(this, len);
}

template<class _CharT, class _Traits, class _Alloc>
void basic_string<_CharT,_Traits,_Alloc>::_M_assign(const basic_string& other) {
    if (this != &other) {
        const size_type rsize = str_len((void*)&other);
        const size_type cap = str_cap(this);
        if (rsize > cap) {
            size_type nc = rsize;
            _CharT* tmp = (_CharT*)_M_create(nc, cap);
            _M_dispose();
            SL(this)->p = (char*)tmp;
            SL(this)->cap = nc;
        }
        if (rsize) memcpy(SL(this)->p, SL((void*)&other)->p, rsize);
        str_set_length(this, rsize);
    }
}

template<class _CharT, class _Traits, class _Alloc>
void basic_string<_CharT,_Traits,_Alloc>::_M_replace(size_type pos, size_type len1, const _CharT* s, size_type len2) {
    const size_type old_size = str_len(this);
    const size_type new_size = old_size + len2 - len1;
    const size_type how_much = old_size - pos - len1;
    if (new_size <= str_cap(this)) {
        _CharT* p = SL(this)->p + pos;
        if (str_disjunct(this, s)) {
            if (how_much && len1 != len2) memmove(p + len2, p + len1, how_much);
            if (len2) memcpy(p, s, len2);
        } else {
            // in-place overlapping replace (mirrors _M_replace_cold)
            if (len2 && len2 <= len1) memmove(p, s, len2);
            if (how_much && len1 != len2) memmove(p + len2, p + len1, how_much);
            if (len2 > len1) {
                if (s + len2 <= p + len1) memmove(p, s, len2);
                else if (s >= p + len1) {
                    const size_type poff = (s - p) + (len2 - len1);
                    memcpy(p, p + poff, len2);
                } else {
                    const size_type nleft = (p + len1) - s;
                    memmove(p, s, nleft);
                    memcpy(p + nleft, p + len2, len2 - nleft);
                }
            }
        }
    } else {
        _M_mutate(pos, len1, s, len2);
    }
    str_set_length(this, new_size);
}

template<class _CharT, class _Traits, class _Alloc>
typename basic_string<_CharT,_Traits,_Alloc>::size_type
basic_string<_CharT,_Traits,_Alloc>::find(_CharT c, size_type pos) const {
    const size_type sz = str_len((void*)this);
    if (pos >= sz) return (size_type)-1;
    for (size_type i = pos; i < sz; i++)
        if (SL((void*)this)->p[i] == c) return i;
    return (size_type)-1;
}

} // namespace __cxx11
} // namespace std

// emit the char instantiation with the exact libstdc++ mangled names
template class std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >;