// softfloat.h + sfexpr.h unit test (standalone)
#include "lib/softfloat.h"
#include "lib/sfexpr.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace nefu::sf;
using namespace nefu::sfexpr;

static int failures = 0;

static void check_f(const char* expr, double expect, int mode = 0) {
    float out = 0;
    bool derr = false;
    int perr = 0, epos = 0;
    if (!sfexpr_parse(expr, &perr, &epos)) { printf("FAIL parse %-22s err=%d@%d\n", expr, perr, epos); failures++; return; }
    if (!sfexpr_eval_no_x(expr, &out, &derr, mode)) { printf("FAIL eval  %-22s domain\n", expr); failures++; return; }
    if (fabs((double)out - expect) > 1e-4 * (fabs(expect) + 1.0)) {
        printf("FAIL val   %-22s got=%.7g expect=%.7g\n", expr, (double)out, expect);
        failures++;
    }
}
static void check_dom(const char* expr) {
    float out = 0; bool derr = false;
    if (sfexpr_eval_no_x(expr, &out, &derr, 0)) { printf("FAIL dom  %-22s should error\n", expr); failures++; }
}
static void check_str(float v, const char* expect, int digits) {
    char b[64];
    sf_to_str(b, sizeof(b), v, digits);
    if (strcmp(b, expect) != 0) { printf("FAIL str  %-18s %s != %s\n", expect, b, expect); failures++; }
}

int main() {
    // basic math
    check_f("2+3*4", 14);
    check_f("(2+3)*4", 20);
    check_f("10/4", 2.5);
    check_f("1/3", 0.3333333);
    check_f("2^10", 1024);
    check_f("2^0.5", 1.41421356);
    check_f("2^3^2", 512);
    check_f("(-2)^3", -8);
    check_f("sqrt(2)", 1.41421356);
    check_f("pi", 3.14159265);
    check_f("e", 2.71828183);
    check_f("abs(-5.5)", 5.5);
    check_f("floor(2.7)", 2);
    check_f("ceil(2.1)", 3);
    // trig radians
    check_f("sin(0)", 0);
    check_f("cos(0)", 1);
    check_f("sin(pi/2)", 1);
    check_f("cos(pi)", -1);
    check_f("tan(pi/4)", 1);
    check_f("asin(1)", 1.57079633);
    check_f("acos(0)", 1.57079633);
    check_f("atan(1)", 0.78539816);
    check_f("sin(100)", sin(100.0));      // range reduction
    check_f("cos(200)", cos(200.0));
    // trig degrees
    check_f("sin(90)", 1, 1);
    check_f("cos(60)", 0.5, 1);
    check_f("asin(1)", 90, 1);
    check_f("tan(45)", 1, 1);
    // exp/log
    check_f("exp(1)", 2.71828183);
    check_f("ln(e)", 1);
    check_f("log(100)", 2);
    check_f("ln(0.5)", -0.69314718);
    check_f("exp(ln(10))", 10);
    check_f("log(1e5)", 5);
    check_f("log10(1000)", 3);
    // implicit multiplication + variable
    {
        float out = 0; bool derr = false;
        if (!sfexpr_eval("2x", 4.0f, &out, &derr, 0) || derr || out != 8.0f) { printf("FAIL 2x(x=4) got=%g\n", (double)out); failures++; }
        if (!sfexpr_eval("x^2+1", 3.0f, &out, &derr, 0) || derr || out != 10.0f) { printf("FAIL x^2+1(x=3) got=%g\n", (double)out); failures++; }
    }
    // scientific notation literal
    check_f("1e9", 1e9);
    check_f("2.5e-3", 0.0025);
    check_f("1.5e2+3", 153);
    // domain errors
    check_dom("1/0");
    check_dom("sqrt(-1)");
    check_dom("ln(-1)");
    check_dom("log(0)");
    check_dom("asin(2)");
    check_dom("(-2)^0.5");
    // parse errors
    int perr = ERR_NONE, epos = 0;
    if (sfexpr_parse("sin(", &perr, &epos)) { printf("FAIL parse sin( should fail\n"); failures++; }
    if (sfexpr_parse("2+*3", &perr, &epos)) { printf("FAIL parse 2+*3 should fail\n"); failures++; }
    if (!sfexpr_parse("2+3", &perr, &epos)) { printf("FAIL parse 2+3 should pass\n"); failures++; }

    // ---- formatting ----
    check_str(0.0f, "0", 7);
    check_str(5.0f, "5", 7);
    check_str(-5.0f, "-5", 7);
    check_str(2.5f, "2.5", 7);
    check_str(-2.5f, "-2.5", 7);
    check_str(1.0f/3.0f, "0.3333333", 7);   // float(1/3) == 0.33333334 -> 7 sig digits
    check_str(1.0f/3.0f, "0.333333", 6);
    check_str(0.0005f, "0.0005", 7);
    check_str(0.0000005f, "5e-7", 7);
    check_str(12345.678f, "12345.68", 7);
    check_str(123456789.0f, "1.234568e8", 7);
    check_str(1.0e9f, "1e9", 7);
    check_str(1.5e10f, "1.5e10", 7);
    check_str(5.0e-7f, "5e-7", 7);
    check_str(1.23456789f, "1.234568", 7);
    check_str(0.1f, "0.1", 7);
    check_str(3.14159265f, "3.141593", 7);
    check_str(100.0f, "100", 7);
    check_str(123.456f, "123.456", 6);
    check_str(-0.00001234f, "-0.00001234", 7);
    check_str(2.0f/7.0f, "0.2857143", 7);

    // ---- atof ----
    if (sf_atof("2.5") != 2.5f) { printf("FAIL atof 2.5\n"); failures++; }
    if (sf_atof("-3") != -3.0f) { printf("FAIL atof -3\n"); failures++; }
    if (sf_atof("1e9") != 1e9f) { printf("FAIL atof 1e9\n"); failures++; }
    if (fabs(sf_atof("2.5e-3") - 0.0025f) > 1e-8f) { printf("FAIL atof 2.5e-3 got %g\n", (double)sf_atof("2.5e-3")); failures++; }
    if (sf_atof(".5") != 0.5f) { printf("FAIL atof .5\n"); failures++; }
    if (sf_atof("1.5E2") != 150.0f) { printf("FAIL atof 1.5E2\n"); failures++; }

    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
