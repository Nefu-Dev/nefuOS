# -*- coding: utf-8 -*-
import sys
p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
raw = open(p, 'rb').read()
bom = raw[:3] == b'\xef\xbb\xbf'
s = raw.decode('utf-8-sig')

def rep(old, new):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit('NOT UNIQUE (%d): %r' % (n, old[:60]))
    s = s.replace(old, new)

rep('#include "../gfxlib/gfxlib_all.h"\n#include "../sys/sha256.h"',
    '#include "../gfxlib/gfxlib_all.h"\n#include "../textlib/text_all.h"\n#include "../sys/sha256.h"')

rep('// ===================== registry =====================',
    'static void cmd_texttest(TermState* t, int argc, const char** argv) {\n'
    '    (void)argc; (void)argv;\n'
    '    // run every text library self test and report a combined verdict\n'
    '    int lev_f = text::levenshtein_self_test();\n'
    '    int lcs_f = text::lcs_self_test();\n'
    '    int sea_f = text::search_self_test();\n'
    '    int aho_f = text::aho_self_test();\n'
    '    int tok_f = text::token_self_test();\n'
    '    int ngr_f = text::ngram_self_test();\n'
    '    int reg_f = text::regex_self_test();\n'
    '    int dif_f = text::diff_self_test();\n'
    '    char out[200];\n'
    '    ksprintf(out, sizeof(out),\n'
    '             "textlib self tests: lev=%d lcs=%d search=%d aho=%d token=%d ngram=%d regex=%d diff=%d failures",\n'
    '             lev_f, lcs_f, sea_f, aho_f, tok_f, ngr_f, reg_f, dif_f);\n'
    '    term_ext_print(t, out);\n'
    '    if (lev_f + lcs_f + sea_f + aho_f + tok_f + ngr_f + reg_f + dif_f == 0)\n'
    '        term_ext_print(t, "ALL TEXT TESTS PASSED");\n'
    '    else\n'
    '        term_ext_print(t, "FAILURES DETECTED - see numbers above");\n'
    '}\n\n'
    '// ===================== registry =====================')

rep('"nextprime", "extselftest", "algotest", "gfxtest"',
    '"nextprime", "extselftest", "algotest", "gfxtest", "texttest"')

rep('else if (strcmp(a0, "gfxtest") == 0) cmd_gfxtest(t, argc, argv);\n    else term_ext_print(t, "ext: unknown command");',
    'else if (strcmp(a0, "gfxtest") == 0) cmd_gfxtest(t, argc, argv);\n'
    '    else if (strcmp(a0, "texttest") == 0) cmd_texttest(t, argc, argv);\n'
    '    else term_ext_print(t, "ext: unknown command");')

data = s if not bom else '\ufeff' + s
open(p, 'w', encoding='utf-8', newline='').write(data)
print('OK bom=%s size=%d' % (bom, len(data)))
