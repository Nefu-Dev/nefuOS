# -*- coding: utf-8 -*-
import io

# 1. term_ext.cpp
p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''#include "../audlib/aud_all.h"
#include "../textlib/text_all.h"'''
new = '''#include "../audlib/aud_all.h"
#include "../dblib/db_all.h"
#include "../textlib/text_all.h"'''
assert s.count(old) == 1, 'inc'
s = s.replace(old, new)

old = '''static void cmd_audlibtest(TermState* t, int argc, const char** argv) {'''
new = '''static void cmd_dblibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every data-storage library self test and report a combined verdict
    int total = nefu::dbx::db_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "dblib self tests: csv+ini+json+bptree+page+table = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL DATA TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_audlibtest(TermState* t, int argc, const char** argv) {'''
assert s.count(old) == 1, 'cmd'
s = s.replace(old, new)

old = '"gfxmathtest", "audlibtest",'
new = '"gfxmathtest", "audlibtest", "dblibtest",'
assert s.count(old) == 1, 'cmdlist'
s = s.replace(old, new)

old = '    else if (strcmp(a0, "audlibtest") == 0) cmd_audlibtest(t, argc, argv);'
new = '    else if (strcmp(a0, "audlibtest") == 0) cmd_audlibtest(t, argc, argv);\n    else if (strcmp(a0, "dblibtest") == 0) cmd_dblibtest(t, argc, argv);'
assert s.count(old) == 1, 'dispatch'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK term_ext')

# 2. terminal.cpp
p2 = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
with io.open(p2, encoding='utf-8') as f: s2 = f.read()
old = '''            term_print(t, "  - audlib/aud_all.h (wave/synth/env/filter/seq/mixer/delay/reverb/mod/pitch)");'''
new = '''            term_print(t, "  - audlib/aud_all.h (wave/synth/env/filter/seq/mixer/delay/reverb/mod/pitch)");
            term_print(t, "  - dblib/db_all.h (csv/ini/json/bptree/page/table)");'''
assert s2.count(old) == 1, 'help1'
s2 = s2.replace(old, new)
old2 = '''            term_print(t, "  - including audlib/aud_all.h");'''
new2 = '''            term_print(t, "  - including audlib/aud_all.h");
            term_print(t, "  - including dblib/db_all.h");'''
assert s2.count(old2) == 1, 'help2'
s2 = s2.replace(old2, new2)
with io.open(p2, 'w', encoding='utf-8', newline='\n') as f: f.write(s2)
print('OK terminal.cpp')

# 3. build scripts
db_block = '''    "core\\dblib\\csv.cpp", "core\\dblib\\ini.cpp", "core\\dblib\\jsonstore.cpp",
    "core\\dblib\\bptree.cpp", "core\\dblib\\page.cpp", "core\\dblib\\table.cpp",
'''
for pb in [r'D:\mycppos1\nefuOS\tools\build_host_only.ps1',
           r'D:\mycppos1\nefuOS\tools\build_iso.ps1']:
    with io.open(pb, encoding='utf-8') as f: sb = f.read()
    ob = '    "core\\audlib\\modulate.cpp",\n'
    assert sb.count(ob) == 1, pb
    sb = sb.replace(ob, ob + db_block)
    with io.open(pb, 'w', encoding='utf-8', newline='\n') as f: f.write(sb)
    print('OK', pb)
