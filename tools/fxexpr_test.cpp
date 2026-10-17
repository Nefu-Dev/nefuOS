// fxexpr.h unit test (standalone, no nefuOS deps beyond softmath)
#include "lib/fxexpr.h"   // -I core
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace nefu::fxexpr;
using nefu::fx::fix;
using nefu::fx::itofix;

static int failures = 0;
static void check(const char* expr, double expect_x, double expect_val, int mode = 0) {
    fix out = 0;
    bool derr = false;
    int perr = 0, epos = 0;
    bool pok = fxexpr_parse(expr, &perr, &epos);
    if (!pok) { printf("FAIL parse  %-24s err=%d@%d\n", expr, perr, epos); failures++; return; }
    fix x = (fix)(expect_x * 65536.0);
    bool ok = fxexpr_eval(expr, x, &out, &derr, mode);
    if (!ok || derr) { printf("FAIL eval   %-24s (domain err)\n", expr); failures++; return; }
    double got = (double)out / 65536.0;
    if (fabs(got - expect_val) > 0.002) {
        printf("FAIL value  %-24s x=%.3f  got=%.5f expect=%.5f\n", expr, expect_x, got, expect_val);
        failures++;
    }
}
static void check_dom(const char* expr) {
    fix out = 0;
    bool derr = false;
    bool ok = fxexpr_eval_no_x(expr, &out, &derr, 0);
    if (ok && !derr) { printf("FAIL domain %-24s should error, got %.4f\n", expr, (double)out / 65536.0); failures++; }
}
static void check_str(fix v, const char* expect, int mindec, int maxdec) {
    char b[64];
    fix_to_str(b, sizeof(b), v, mindec, maxdec);
    if (strcmp(b, expect) != 0) { printf("FAIL str    %s != %s (v=%d)\n", b, expect, (int)v); failures++; }
}

int main() {
    // basics
    check("2+3*4", 0, 14);
    check("(2+3)*4", 0, 20);
    check("10/4", 0, 2.5);
    check("1-2+3", 0, 2);
    check("--5", 0, 5);
    check("-2^2", 0, -4);       // unary minus binds looser than ^
    check("2^3^2", 0, 512);     // right associative
    check("2^0.5", 0, 1.41421);
    check("sqrt(16)", 0, 4);
    check("sqrt(2)", 0, 1.41421);
    // x variable
    check("x+1", 2, 3);
    check("2x", 3, 6);               // implicit multiply
    check("2(x+1)", 3, 8);
    check("(x+1)(x-1)", 3, 8);
    check("3pi", 0, 3 * 3.14159);
    // trig (radians)
    check("sin(0)", 0, 0);
    check("cos(0)", 0, 1);
    check("sin(pi/2)", 0, 1);
    check("cos(pi)", 0, -1);
    check("tan(pi/4)", 0, 1);
    check("asin(1)", 0, 1.5707963);
    check("acos(0)", 0, 1.5707963);
    check("atan(1)", 0, 0.785398);
    // trig (degrees)
    check("sin(90)", 0, 1, 1);
    check("cos(60)", 0, 0.5, 1);
    check("asin(1)", 0, 90, 1);
    // exp/log
    check("exp(1)", 0, 2.7182818);
    check("ln(e)", 0, 1);
    check("log(100)", 0, 2);
    check("ln(exp(2))", 0, 2);
    check("abs(-5)", 0, 5);
    check("floor(2.7)", 0, 2);
    check("ceil(2.1)", 0, 3);
    check("0.5+0.25", 0, 0.75);
    check("1.5*2", 0, 3);
    check("100/3", 0, 33.333);
    check("2x^2", 3, 18);
    check("sin(x)", 1.5707963, 1);   // sin at x=pi/2
    // domain errors
    check_dom("1/0");
    check_dom("sqrt(-1)");
    check_dom("ln(-1)");
    check_dom("log(0)");
    check_dom("(-2)^0.5");
    check("(-2)^3", 0, -8);   // integer exponent of negative base
    check("(-2)^2", 0, 4);
    // parse errors
    int perr = ERR_NONE, epos = 0;
    if (fxexpr_parse("sin(", &perr, &epos)) { printf("FAIL parse sin( should fail\n"); failures++; }
    if (fxexpr_parse("2+*3", &perr, &epos)) { printf("FAIL parse 2+*3 should fail\n"); failures++; }
    if (fxexpr_parse("foo(2)", &perr, &epos)) { printf("FAIL parse foo(2) should fail\n"); failures++; }
    if (!fxexpr_parse("2+3", &perr, &epos)) { printf("FAIL parse 2+3 should pass\n"); failures++; }

    // format
    check_str(itofix(5), "5", 0, 3);
    check_str(itofix(-5), "-5", 0, 3);
    check_str(itofix(2) + (fix)(0.5 * 65536), "2.5", 0, 3);
    check_str(itofix(-2) - (fix)(0.5 * 65536), "-2.5", 0, 3);
    check_str(itofix(0), "0", 0, 3);
    check_str((fix)(1.5 * 65536), "1.5", 0, 3);
    check_str((fix)(1.23456 * 65536), "1.235", 0, 3);
    check_str(itofix(10), "10", 0, 1);

    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
