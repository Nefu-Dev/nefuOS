# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''static void cmd_mathlibtest(TermState* t, int argc, const char** argv) {
    (void)t; (void)argc; (void)argv;
    nefu::term::println("mathlib self tests running...");
    int total = nefu::mathx::math_all_self_test();
    nefu::term::println("mathlib self tests total failures = %d", total);
}'''
new = '''static void cmd_mathlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every math library self test and report a combined verdict
    int total = nefu::mathx::math_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "mathlib self tests: bigint+rational+complex+matrix+fft+poly+stat+regress+prime+rand+vec2+gcd = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL MATH TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}'''
assert s.count(old) == 1, 'mathlibtest'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK')
