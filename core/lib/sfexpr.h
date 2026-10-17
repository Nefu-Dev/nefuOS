// nefuOS float expression parser/evaluator.
// Same grammar as core/lib/fxexpr.h but evaluates with single-precision
// `float` via the soft-float library (core/lib/softfloat.h), so it runs on
// the host AND the bare-metal kernel (GCC soft-float). Use this when you
// need a huge range (1e-38 .. 1e38) or scientific notation display.
//
// Grammar:
//   expr     := term (('+' | '-') term)*
//   term     := unary (('*' | '/' | implicit) unary)*
//   unary    := '-' unary | power
//   power    := primary ('^' unary)?
//   primary  := number | 'x' | const | func '(' expr (',' expr)? ')' | '(' expr ')'
//
// Functions: sin cos tan asin acos atan sqrt exp ln log abs floor ceil
// Constants: pi e
// mode: 0 = radians, 1 = degrees
#pragma once
#include "softfloat.h"
#include <stddef.h>

namespace nefu {
namespace sfexpr {

enum {
    ERR_NONE = 0,
    ERR_SYNTAX,
    ERR_UNKNOWN_FUNC,
    ERR_PAREN,
    ERR_EMPTY,
    ERR_TOO_LONG
};

inline bool sfexpr_parse(const char* s, int* err, int* err_pos);
inline bool sfexpr_eval(const char* s, float x, float* out, bool* domain_err, int mode);
inline bool sfexpr_eval_no_x(const char* s, float* out, bool* domain_err, int mode);

namespace detail {

struct Parser {
    const char* s;
    int pos;
    int err;
    int mode;
    float x;

    Parser(const char* str, int m, float xv) : s(str), pos(0), err(ERR_NONE), mode(m), x(xv) {}

    void skip_ws() { while (s[pos] == ' ' || s[pos] == '\t') pos++; }
    bool at_end() { skip_ws(); return s[pos] == 0; }
    bool is_digit(char c) { return c >= '0' && c <= '9'; }

    float parse_number() {
        float v = 0.0f;
        bool any = false;
        while (is_digit(s[pos])) { v = v * 10.0f + (float)(s[pos] - '0'); pos++; any = true; }
        if (s[pos] == '.') {
            pos++;
            float div = 1.0f;
            while (is_digit(s[pos])) { v = v * 10.0f + (float)(s[pos] - '0'); div *= 10.0f; pos++; any = true; }
            v /= div;
        }
        if (s[pos] == 'e' || s[pos] == 'E') {
            // scientific notation literal (e.g. 1e9) — must not collide with
            // the constant 'e', which is an identifier, so only after digits
            pos++;
            bool neg = false;
            if (s[pos] == '-') { neg = true; pos++; }
            else if (s[pos] == '+') pos++;
            int e = 0;
            while (is_digit(s[pos])) { e = e * 10 + (s[pos] - '0'); if (e > 99) e = 99; pos++; }
            float p10 = 1.0f;
            for (int i = 0; i < e; i++) p10 *= 10.0f;
            v *= neg ? 1.0f / p10 : p10;
        }
        if (!any) { err = ERR_SYNTAX; return 0.0f; }
        if (v > 3.0e38f || v < -3.0e38f) { err = ERR_TOO_LONG; return 0.0f; }
        return v;
    }

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

    static bool str_eq(const char* a, const char* b) {
        while (*a && *b) { if (*a != *b) return false; a++; b++; }
        return *a == *b;
    }

    bool call_func(const char* name, float arg, float* out, bool* domain_err) {
        using namespace sf;
        float a = arg;
        float r = 0.0f;
        bool deg = mode == 1;
        if (str_eq(name, "sin"))  r = sf_sin(deg ? a * SF_PI / 180.0f : a);
        else if (str_eq(name, "cos")) r = sf_cos(deg ? a * SF_PI / 180.0f : a);
        else if (str_eq(name, "tan")) r = sf_tan(deg ? a * SF_PI / 180.0f : a);
        else if (str_eq(name, "asin")) {
            if (a < -1.0f || a > 1.0f) { *domain_err = true; *out = 0.0f; return true; }
            r = sf_asin(a);
            if (deg) r = r * 180.0f / SF_PI;
        } else if (str_eq(name, "acos")) {
            if (a < -1.0f || a > 1.0f) { *domain_err = true; *out = 0.0f; return true; }
            r = sf_acos(a);
            if (deg) r = r * 180.0f / SF_PI;
        } else if (str_eq(name, "atan")) {
            r = sf_atan(a);
            if (deg) r = r * 180.0f / SF_PI;
        } else if (str_eq(name, "sqrt")) {
            if (a < 0.0f) { *domain_err = true; *out = 0.0f; return true; }
            r = sf_sqrt(a);
        } else if (str_eq(name, "exp")) {
            r = sf_exp(a);
        } else if (str_eq(name, "ln") || str_eq(name, "log") || str_eq(name, "log10")) {
            if (a <= 0.0f) { *domain_err = true; *out = 0.0f; return true; }
            r = str_eq(name, "ln") ? sf_log(a) : sf_log10(a);
        } else if (str_eq(name, "abs")) {
            r = sf_abs(a);
        } else if (str_eq(name, "floor")) {
            r = sf_floor(a);
        } else if (str_eq(name, "ceil")) {
            r = sf_ceil(a);
        } else {
            err = ERR_UNKNOWN_FUNC;
            return false;
        }
        *out = r;
        return true;
    }

    bool parse_expr(float* out, bool* domain_err) {
        float a;
        if (!parse_term(&a, domain_err)) return false;
        for (;;) {
            skip_ws();
            char c = s[pos];
            if (c == '+') { pos++; float b; if (!parse_term(&b, domain_err)) return false; a += b; }
            else if (c == '-') { pos++; float b; if (!parse_term(&b, domain_err)) return false; a -= b; }
            else break;
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

    bool parse_term(float* out, bool* domain_err) {
        float a;
        if (!parse_unary(&a, domain_err)) return false;
        for (;;) {
            skip_ws();
            char c = s[pos];
            if (c == '*' || c == '/') {
                pos++;
                float b;
                if (!parse_unary(&b, domain_err)) return false;
                if (c == '*') a *= b;
                else {
                    if (b == 0.0f) { *domain_err = true; *out = 0.0f; return true; }
                    a /= b;
                }
            } else if (is_implicit_start()) {
                float b;
                if (!parse_unary(&b, domain_err)) return false;
                a *= b;
            } else break;
        }
        *out = a;
        return true;
    }

    bool parse_unary(float* out, bool* domain_err) {
        skip_ws();
        if (s[pos] == '-') {
            pos++;
            float a;
            if (!parse_unary(&a, domain_err)) return false;
            *out = -a;
            return true;
        }
        if (s[pos] == '+') { pos++; return parse_unary(out, domain_err); }
        return parse_power(out, domain_err);
    }

    bool parse_power(float* out, bool* domain_err) {
        float base;
        if (!parse_primary(&base, domain_err)) return false;
        skip_ws();
        if (s[pos] == '^') {
            pos++;
            float e;
            if (!parse_unary(&e, domain_err)) return false;
            if (base < 0.0f && e != sf::sf_floor(e)) { *domain_err = true; *out = 0.0f; return true; }
            if (base == 0.0f && e < 0.0f) { *domain_err = true; *out = 0.0f; return true; }
            if (e > 120.0f || e < -120.0f) { *domain_err = true; *out = 0.0f; return true; }
            *out = sf::sf_pow(base, e);
            return true;
        }
        *out = base;
        return true;
    }

    bool parse_primary(float* out, bool* domain_err) {
        skip_ws();
        char c = s[pos];
        if (is_digit(c) || c == '.') {
            *out = parse_number();
            return err == ERR_NONE;
        }
        if (c == '(') {
            pos++;
            float a;
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
            if (id == ID_CONST_PI) { *out = sf::SF_PI; return true; }
            if (id == ID_CONST_E) { *out = sf::SF_E; return true; }
            if (id == ID_FUNC) {
                skip_ws();
                if (s[pos] != '(') { err = ERR_PAREN; return false; }
                pos++;
                float a;
                if (!parse_expr(&a, domain_err)) return false;
                skip_ws();
                if (s[pos] != ')') { err = ERR_PAREN; return false; }
                pos++;
                float r;
                if (!call_func(name, a, &r, domain_err)) return false;
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

inline bool sfexpr_parse(const char* s, int* err, int* err_pos) {
    if (!s || s[0] == 0) {
        if (err) *err = ERR_EMPTY;
        if (err_pos) *err_pos = 0;
        return false;
    }
    detail::Parser p(s, 0, 0.0f);
    bool domain = false;
    float dummy = 0.0f;
    bool ok = p.parse_expr(&dummy, &domain);
    if (ok && !p.at_end()) { p.err = ERR_SYNTAX; ok = false; }
    if (err) *err = p.err;
    if (err_pos) *err_pos = p.pos;
    return ok;
}

inline bool sfexpr_eval(const char* s, float x, float* out, bool* domain_err, int mode) {
    *domain_err = false;
    detail::Parser p(s, mode, x);
    bool ok = p.parse_expr(out, domain_err);
    return ok && *domain_err == false;
}

inline bool sfexpr_eval_no_x(const char* s, float* out, bool* domain_err, int mode) {
    return sfexpr_eval(s, 0.0f, out, domain_err, mode);
}

} // namespace sfexpr
} // namespace nefu
