# -*- coding: utf-8 -*-
import io

# 1. term_ext.cpp
p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''#include "../gfxmath/gfx_all.h"
#include "../textlib/text_all.h"'''
new = '''#include "../gfxmath/gfx_all.h"
#include "../audlib/aud_all.h"
#include "../textlib/text_all.h"'''
assert s.count(old) == 1, 'inc'
s = s.replace(old, new)

old = '''static void cmd_gfxmathtest(TermState* t, int argc, const char** argv) {'''
new = '''static void cmd_audlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every audio library self test and report a combined verdict
    int total = nefu::audx::aud_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "audlib self tests: wave+synth+env+filter+seq+mixer+delay+reverb+mod+pitch = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL AUDIO TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_gfxmathtest(TermState* t, int argc, const char** argv) {'''
assert s.count(old) == 1, 'cmd'
s = s.replace(old, new)

old = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest", "crypttest", "comptest", "complibtest", "mathlibtest", "simlibtest", "gfxmathtest",'
new = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest", "crypttest", "comptest", "complibtest", "mathlibtest", "simlibtest", "gfxmathtest", "audlibtest",'
assert s.count(old) == 1, 'cmdlist'
s = s.replace(old, new)

old = '    else if (strcmp(a0, "gfxmathtest") == 0) cmd_gfxmathtest(t, argc, argv);'
new = '    else if (strcmp(a0, "gfxmathtest") == 0) cmd_gfxmathtest(t, argc, argv);\n    else if (strcmp(a0, "audlibtest") == 0) cmd_audlibtest(t, argc, argv);'
assert s.count(old) == 1, 'dispatch'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK term_ext')

# 2. terminal.cpp
p2 = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
with io.open(p2, encoding='utf-8') as f: s2 = f.read()
old = '''            term_print(t, "  - gfxmath/gfx_all.h (vec3/mat4/quat/ray/plane/aabb/sphere/camera/mesh/proj/render)");'''
new = '''            term_print(t, "  - gfxmath/gfx_all.h (vec3/mat4/quat/ray/plane/aabb/sphere/camera/mesh/proj/render)");
            term_print(t, "  - audlib/aud_all.h (wave/synth/env/filter/seq/mixer/delay/reverb/mod/pitch)");'''
assert s2.count(old) == 1, 'help1'
s2 = s2.replace(old, new)
old2 = '''            term_print(t, "  - including gfxmath/gfx_all.h");'''
new2 = '''            term_print(t, "  - including gfxmath/gfx_all.h");
            term_print(t, "  - including audlib/aud_all.h");'''
assert s2.count(old2) == 1, 'help2'
s2 = s2.replace(old2, new2)
with io.open(p2, 'w', encoding='utf-8', newline='\n') as f: f.write(s2)
print('OK terminal.cpp')

# 3. build scripts
aud_block = '''    "core\\audlib\\wave.cpp", "core\\audlib\\synth.cpp", "core\\audlib\\env.cpp",
    "core\\audlib\\filter.cpp", "core\\audlib\\seq.cpp", "core\\audlib\\mixer.cpp",
    "core\\audlib\\modulate.cpp",
'''
for pb in [r'D:\mycppos1\nefuOS\tools\build_host_only.ps1',
           r'D:\mycppos1\nefuOS\tools\build_iso.ps1']:
    with io.open(pb, encoding='utf-8') as f: sb = f.read()
    ob = '    "core\\gfxmath\\mesh.cpp", "core\\gfxmath\\proj.cpp",\n'
    assert sb.count(ob) == 1, pb
    sb = sb.replace(ob, ob + aud_block)
    with io.open(pb, 'w', encoding='utf-8', newline='\n') as f: f.write(sb)
    print('OK', pb)
