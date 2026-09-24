# -*- coding: utf-8 -*-
import io

# 1. term_ext.cpp
p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''#include "../mathlib/math_all.h"
#include "../textlib/text_all.h"'''
new = '''#include "../mathlib/math_all.h"
#include "../simlib/sim_all.h"
#include "../textlib/text_all.h"'''
assert s.count(old) == 1, 'inc'
s = s.replace(old, new)

old = '''static void cmd_mathlibtest(TermState* t, int argc, const char** argv) {'''
new = '''static void cmd_simlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every simulation library self test and report a combined verdict
    int total = nefu::simx::sim_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "simlib self tests: life+queue+world+boids+epidemic+traffic+perlin+langton = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL SIM TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_mathlibtest(TermState* t, int argc, const char** argv) {'''
assert s.count(old) == 1, 'cmd'
s = s.replace(old, new)

old = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest", "crypttest", "comptest", "complibtest", "mathlibtest",'
new = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest", "crypttest", "comptest", "complibtest", "mathlibtest", "simlibtest",'
assert s.count(old) == 1, 'cmdlist'
s = s.replace(old, new)

old = '    else if (strcmp(a0, "mathlibtest") == 0) cmd_mathlibtest(t, argc, argv);'
new = '    else if (strcmp(a0, "mathlibtest") == 0) cmd_mathlibtest(t, argc, argv);\n    else if (strcmp(a0, "simlibtest") == 0) cmd_simlibtest(t, argc, argv);'
assert s.count(old) == 1, 'dispatch'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK term_ext')

# 2. terminal.cpp
p2 = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
with io.open(p2, encoding='utf-8') as f: s2 = f.read()
old = '''            term_print(t, "  - mathlib/math_all.h (bigint/rational/complex/matrix/fft/poly/stat/regress/prime/rand/vec2/gcd)");'''
new = '''            term_print(t, "  - mathlib/math_all.h (bigint/rational/complex/matrix/fft/poly/stat/regress/prime/rand/vec2/gcd)");
            term_print(t, "  - simlib/sim_all.h (life/queue/world/boids/epidemic/traffic/perlin/langton)");'''
assert s2.count(old) == 1, 'help1'
s2 = s2.replace(old, new)
old2 = '''            term_print(t, "  - including mathlib/math_all.h");'''
new2 = '''            term_print(t, "  - including mathlib/math_all.h");
            term_print(t, "  - including simlib/sim_all.h");'''
assert s2.count(old2) == 1, 'help2'
s2 = s2.replace(old2, new2)
with io.open(p2, 'w', encoding='utf-8', newline='\n') as f: f.write(s2)
print('OK terminal.cpp')

# 3. build scripts
sim_block = '''    "core\\simlib\\life.cpp", "core\\simlib\\queue.cpp", "core\\simlib\\world.cpp",
    "core\\simlib\\boids.cpp", "core\\simlib\\epidemic.cpp", "core\\simlib\\traffic.cpp",
    "core\\simlib\\perlin.cpp", "core\\simlib\\langton.cpp",
'''
for pb in [r'D:\mycppos1\nefuOS\tools\build_host_only.ps1',
           r'D:\mycppos1\nefuOS\tools\build_iso.ps1']:
    with io.open(pb, encoding='utf-8') as f: sb = f.read()
    ob = '    "core\\mathlib\\random.cpp", "core\\mathlib\\vector2.cpp", "core\\mathlib\\gcd.cpp",\n'
    assert sb.count(ob) == 1, pb
    sb = sb.replace(ob, ob + sim_block)
    with io.open(pb, 'w', encoding='utf-8', newline='\n') as f: f.write(sb)
    print('OK', pb)
