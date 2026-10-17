// nefuOS shared math expression parser/evaluator.
// Built on top of the fx fixed-point library (core/lib/softmath.h, Q16.16),
// so it runs on both the Windows host and the bare-metal kernel (no FPU).
//
// Grammar (recursive descent, implicit multiplication supported):
//   expr     := term (('+' | '-') term)*
//   term     := unary (('*' | '/' | implicit) unary)*
//   unary    := '-' unary | power
//   power    := primary ('^' unary)?          (right associative)
//   primary  := number | 'x' | const | func '(' expr (',' expr)? ')' | '(' expr ')'
//
// Functions: sin cos tan asin acos atan sqrt exp ln log abs floor ceil
// Constants: pi e
// The trig angle mode is 0 = radians, 1 = degrees (fxexpr_eval takes a mode arg).
#pragma once
#include "softmath.h"
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace fxexpr {

// ---- error reporting ----
enum {
    ERR_NONE = 0,
    ERR_SYNTAX,        // unexpected character / malformed expression
    ERR_UNKNOWN_FUNC,  // unknown function name
    ERR_PAREN,         // missing ')' or '('
    ERR_EMPTY,         // empty expression
    ERR_TOO_LONG       // number too large for Q16.16
};

// Parse-only validation: returns false + err/err_pos when the expression has
// a syntax error. Value-level errors (sqrt of a negative, div by zero...) are
// NOT reported here -- use fxexpr_eval's domain_err for those.
bool fxexpr_parse(const char* s, int* err, int* err_pos);

// Evaluate a syntactically valid expression at x (Q16.16).
//   mode : 0 = radians, 1 = degrees (applies to sin/cos/tan/asin/acos/atan)
//   out  : result on success
//   domain_err : set true when a value-level error occurred (div by zero,
//                sqrt(neg), ln(<=0), pow(b<=0), overflow); *out is then
//                undefined but written with a saturating value.
bool fxexpr_eval(const char* s, fx::fix x, fx::fix* out, bool* domain_err, int mode);

// Convenience: evaluate with no 'x' variable (scientific calculator).
bool fxexpr_eval_no_x(const char* s, fx::fix* out, bool* domain_err, int mode);

// Q16.16 -> decimal string. min_dec/max_dec control the fractional width
// (trailing zeros are stripped down to min_dec). e.g. fix_to_str(b,32, 5<<16,0,2) => "5"
void fix_to_str(char* out, size_t n, fx::fix v, int min_dec, int max_dec);

// ---- implementation ----
namespace detail {

struct Parser {
    const char* s;
    int pos;
    int err;
    int mode;          // trig mode
    fx::fix x;         // variable value

    Parser(const char* str, int m, fx::fix xv) : s(str), pos(0), err(ERR_NONE), mode(m), x(xv) {}

    void skip_ws() {
        while (s[pos] == ' ' || s[pos] == '\t') pos++;
    }

    bool at_end() { skip_ws(); return s[pos] == 0; }

    bool is_digit(char c) { return c >= '0' && c <= '9'; }

    // ---- number: integer or decimal fraction -> Q16.16 ----
    fx::fix parse_number() {
        long long ip = 0;         // integer part
        long long fp = 0;         // fraction numerator
        int fl = 0;               // fraction digit count
        bool any = false;
        while (is_digit(s[pos])) {
            ip = ip * 10 + (s[pos] - '0');
            if (ip > 0x7FFFFFFFLL) { err = ERR_TOO_LONG; return 0; }
            pos++;
            any = true;
        }
        if (s[pos] == '.') {
            pos++;
            while (is_digit(s[pos])) {
                fp = fp * 10 + (s[pos] - '0');
                fl++;
                pos++;
                any = true;
            }
        }
        if (!any) { err = ERR_SYNTAX; return 0; }
        // fraction: fp / 10^fl  (clamp to 5 digits)
        while (fl > 5) { fp /= 10; fl--; }
        long long scale = 1;
        for (int i = 0; i < fl; i++) scale *= 10;
        long long v = ip * 65536LL;
        if (scale > 0) v += (fp * 65536LL) / scale;
        if (v > 0x7FFFFFFFLL) { err = ERR_TOO_LONG; return 0; }
        return (fx::fix)v;
    }

    // ---- identifier: function / constant / x ----
    // Returns: 0 = not an identifier; otherwise a small enum.
    enum { ID_X = 1, ID_CONST_PI, ID_CONST_E, ID_FUNC };
    int read_ident(char* name, int cap) {
        skip_ws();
        if (!((s[pos] >= 'a' && s[pos] <= 'z') || (s[pos] >= 'A' && s[pos] <= 'Z'))) return 0;
        int n = 0;
        while ((s[pos] >= 'a' && s[pos] <= 'z') || (s[pos] >= 'A' && s[pos] <= 'Z') ||
               (s[pos] >= '0' && s[pos] <= '9') || s[pos] == '_') {
            if (n < cap - 1) name[n++] = s[pos];
            pos++;
        }
        name[n] = 0;
        if (n == 1 && (name[0] == 'x' || name[0] == 'X')) return ID_X;
        if (n == 2 && name[0] == 'p' && name[1] == 'i') return ID_CONST_PI;
        if (n == 1 && name[0] == 'e') return ID_CONST_E;
        return ID_FUNC;
    }

    // named functions
    bool call_func(const char* name, fx::fix arg, bool arg_is_deg_input, fx::fix* out, bool* domain_err) {
        using namespace fx;
        fix a = arg;
        fix r = 0;
        bool deg = mode == 1;
        if (str_eq(name, "sin")) {
            r = fx_sin(deg ? fx_deg2rad(a) : a);
        } else if (str_eq(name, "cos")) {
            r = fx_cos(deg ? fx_deg2rad(a) : a);
        } else if (str_eq(name, "tan")) {
            r = fx_tan(deg ? fx_deg2rad(a) : a);
        } else if (str_eq(name, "asin")) {
            if (a < -FX_ONE || a > FX_ONE) { *domain_err = true; *out = 0; return true; }
            r = fx_asin(a);
            if (deg) r = fx_rad2deg(r);
        } else if (str_eq(name, "acos")) {
            if (a < -FX_ONE || a > FX_ONE) { *domain_err = true; *out = 0; return true; }
            r = fx_acos(a);
            if (deg) r = fx_rad2deg(r);
        } else if (str_eq(name, "atan")) {
            r = fx_atan(a);
            if (deg) r = fx_rad2deg(r);
        } else if (str_eq(name, "sqrt")) {
            if (a < 0) { *domain_err = true; *out = 0; return true; }
            r = fx_sqrt(a);
        } else if (str_eq(name, "exp")) {
            r = fx_exp(a);
        } else if (str_eq(name, "ln") || str_eq(name, "log")) {
            if (a <= 0) { *domain_err = true; *out = 0; return true; }
            r = str_eq(name, "ln") ? fx_ln(a) : fx_log10(a);
        } else if (str_eq(name, "abs")) {
            r = fx_abs(a);
        } else if (str_eq(name, "floor")) {
            r = fx_floor(a);
        } else if (str_eq(name, "ceil")) {
            r = fx_ceil(a);
        } else {
            err = ERR_UNKNOWN_FUNC;
            return false;
        }
        (void)arg_is_deg_input;
        *out = r;
        return true;
    }

    static bool str_eq(const char* a, const char* b) {
        while (*a && *b) { if (*a != *b) return false; a++; b++; }
        return *a == *b;
    }

    // ---- recursive descent ----
    bool parse_expr(fx::fix* out, bool* domain_err) {
        fx::fix a;
        if (!parse_term(&a, domain_err)) return false;
        for (;;) {
            skip_ws();
            char c = s[pos];
            if (c == '+') { pos++; fx::fix b; if (!parse_term(&b, domain_err)) return false; a += b; }
            else if (c == '-') { pos++; fx::fix b; if (!parse_term(&b, domain_err)) return false; a -= b; }
            else break;
        }
        *out = a;
        return true;
    }

    bool parse_term(fx::fix* out, bool* domain_err) {
        fx::fix a;
        if (!parse_unary(&a, domain_err)) return false;
        for (;;) {
            skip_ws();
            char c = s[pos];
            if (c == '*' || c == '/') {
                pos++;
                fx::fix b;
                if (!parse_unary(&b, domain_err)) return false;
                if (c == '*') a = fx::fx_mul(a, b);
                else {
                    if (b == 0) { *domain_err = true; *out = 0; return true; }
                    a = fx::fx_div(a, b);
                }
            } else if (is_implicit_start()) {
                // implicit multiplication: 2x, 2sin(x), (x)(x+1), 3pi ...
                fx::fix b;
                if (!parse_unary(&b, domain_err)) return false;
                a = fx::fx_mul(a, b);
            } else break;
        }
        *out = a;
        return true;
    }

    bool is_implicit_start() {
        skip_ws();
        char c = s[pos];
        if (is_digit(c) || c == '.' || c == '(' || c == 'x' || c == 'X') return true;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
        return false;
    }

    bool parse_unary(fx::fix* out, bool* domain_err) {
        skip_ws();
        if (s[pos] == '-') {
            pos++;
            fx::fix a;
            if (!parse_unary(&a, domain_err)) return false;
            *out = (fx::fix)-a;
            return true;
        }
        if (s[pos] == '+') {
            pos++;
            return parse_unary(out, domain_err);
        }
        return parse_power(out, domain_err);
    }

    bool parse_power(fx::fix* out, bool* domain_err) {
        fx::fix base;
        if (!parse_primary(&base, domain_err)) return false;
        skip_ws();
        if (s[pos] == '^') {
            pos++;
            fx::fix e;
            if (!parse_unary(&e, domain_err)) return false;
            // integer exponent (|e| <= 64): exact repeated multiplication
            fx::fix e_round = (e + (fx::fix)0x8000) & ~(fx::fix)0xFFFF;
            if (e_round == e && e >= fx::itofix(-64) && e <= fx::itofix(64)) {
                bool neg = e < 0;
                int n = fx::fixtoi(e_round);
                if (n < 0) n = -n;
                fx::fix r = fx::FX_ONE;
                fx::fix b = base;
                if (b == 0 && neg) { *domain_err = true; *out = 0; return true; }
                for (int i = 0; i < n && i < 64; i++) r = fx::fx_mul(r, b);
                if (neg) r = fx::fx_div(fx::FX_ONE, r);
                *out = r;
                return true;
            }
            if (base <= 0) { *domain_err = true; *out = 0; return true; }
            if (e > fx::itofix(32) || e < fx::itofix(-32)) { *domain_err = true; *out = 0; return true; }
            *out = fx::fx_pow(base, e);
            return true;
        }
        *out = base;
        return true;
    }

    bool parse_primary(fx::fix* out, bool* domain_err) {
        skip_ws();
        char c = s[pos];
        if (is_digit(c) || c == '.') {
            *out = parse_number();
            return err == ERR_NONE;
        }
        if (c == '(') {
            pos++;
            fx::fix a;
            if (!parse_expr(&a, domain_err)) return false;
            skip_ws();
            if (s[pos] != ')') { err = ERR_PAREN; return false; }
            pos++;
            *out = a;
            return true;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            char name[24];
            int id = read_ident(name, sizeof(name));
            if (id == ID_X) { *out = x; return true; }
            if (id == ID_CONST_PI) { *out = fx::FX_PI; return true; }
            if (id == ID_CONST_E) { *out = fx::FX_E; return true; }
            if (id == ID_FUNC) {
                skip_ws();
                if (s[pos] != '(') { err = ERR_PAREN; return false; }
                pos++;
                fx::fix a;
                if (!parse_expr(&a, domain_err)) return false;
                skip_ws();
                if (s[pos] != ')') { err = ERR_PAREN; return false; }
                pos++;
                fx::fix r;
                if (!call_func(name, a, false, &r, domain_err)) return false;
                *out = r;
                return true;
            }
            err = ERR_SYNTAX;
            return false;
        }
        err = ERR_SYNTAX;
        return false;
    }
};

} // namespace detail

inline bool fxexpr_parse(const char* s, int* err, int* err_pos) {
    if (!s || s[0] == 0) {
        if (err) *err = ERR_EMPTY;
        if (err_pos) *err_pos = 0;
        return false;
    }
    detail::Parser p(s, 0, 0);
    bool domain = false;
    fx::fix dummy = 0;
    bool ok = p.parse_expr(&dummy, &domain);
    if (ok && !p.at_end()) { p.err = ERR_SYNTAX; ok = false; }
    if (err) *err = p.err;
    if (err_pos) *err_pos = p.pos;
    return ok;
}

inline bool fxexpr_eval(const char* s, fx::fix x, fx::fix* out, bool* domain_err, int mode) {
    *domain_err = false;
    detail::Parser p(s, mode, x);
    bool ok = p.parse_expr(out, domain_err);
    return ok && *domain_err == false;
}

inline bool fxexpr_eval_no_x(const char* s, fx::fix* out, bool* domain_err, int mode) {
    return fxexpr_eval(s, fx::itofix(0), out, domain_err, mode);
}

// ---- Q16.16 -> decimal string ----
inline void fix_to_str(char* out, size_t n, fx::fix v, int min_dec, int max_dec) {
    if (!out || n == 0) return;
    if (n == 1) { out[0] = 0; return; }
    if (max_dec < 0) max_dec = 0;
    if (max_dec > 6) max_dec = 6;
    if (min_dec > max_dec) min_dec = max_dec;
    bool neg = v < 0;
    unsigned long long av = neg ? (unsigned long long)(-(long long)v) : (unsigned long long)v;
    unsigned long long scale = 1;
    for (int i = 0; i < max_dec; i++) scale *= 10;
    unsigned long long ip = av >> 16;
    // decimal fraction: (frac16 * scale) / 65536, rounded half-up
    unsigned long long frac16 = av & 0xFFFF;
    unsigned long long frac = (frac16 * scale) / 0x10000ULL;
    unsigned long long rem  = (frac16 * scale) % 0x10000ULL;
    if (rem * 2 >= 0x10000ULL) frac++;
    if (frac >= scale) { frac -= scale; ip++; }
    // integer digits
    char ibuf[24];
    int ipn = 0;
    if (ip == 0) ibuf[ipn++] = '0';
    while (ip > 0 && ipn < 23) { ibuf[ipn++] = (char)('0' + (ip % 10)); ip /= 10; }
    size_t wi = 0;
    if (neg && wi < n - 1) out[wi++] = '-';
    for (int i = ipn - 1; i >= 0 && wi < n - 1; i--) out[wi++] = ibuf[i];
    // fraction: strip trailing zeros down to min_dec
    if (max_dec > 0) {
        int dec = max_dec;
        unsigned long long t = frac;
        while (dec > min_dec && (t % 10) == 0) { t /= 10; dec--; }
        if (dec > 0 && wi < n - 1) out[wi++] = '.';
        unsigned long long dscale = scale;
        for (int i = 0; i < dec && wi < n - 1; i++) {
            dscale /= 10;
            out[wi++] = (char)('0' + (int)((frac / dscale) % 10));
        }
    }
    out[wi] = 0;
}

} // namespace fxexpr
} // namespace nefu
