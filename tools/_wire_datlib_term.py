# -*- coding: utf-8 -*-
# Wire datlib into term_ext.cpp: include + cmd_datlibtest + registry + dispatch
import io

path = r"D:\mycppos1\nefuOS\core\apps\term_ext.cpp"
with io.open(path, "r", encoding="utf-8") as f:
    s = f.read()

# 1. include after gfxlib_all.h
old = '#include "../gfxlib/gfxlib_all.h"'
new = '#include "../gfxlib/gfxlib_all.h"\n#include "../datlib/dat_all.h"'
assert s.count(old) == 1, ("include", s.count(old))
s = s.replace(old, new)

# 2. cmd_datlibtest after cmd_texttest
old = '''    if (lev_f + lcs_f + sea_f + aho_f + tok_f + ngr_f + reg_f + dif_f == 0)
        term_ext_print(t, "ALL TEXT TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}'''
new = '''    if (lev_f + lcs_f + sea_f + aho_f + tok_f + ngr_f + reg_f + dif_f == 0)
        term_ext_print(t, "ALL TEXT TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_datlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every data-structure library self test and report a combined verdict
    int v_f  = dt::vec_self_test();
    int r_f  = dt::ringbuf_self_test();
    int b_f  = dt::bitset_self_test();
    int h_f  = dt::hashtab_self_test();
    int rb_f = dt::rbtree_self_test();
    int tp_f = dt::treap_self_test();
    int bt_f = dt::btree_self_test();
    int sg_f = dt::segtree_self_test();
    int fw_f = dt::fenwick_self_test();
    int bl_f = dt::bloom_self_test();
    int lr_f = dt::lru_self_test();
    int sl_f = dt::sortedlist_self_test();
    int iq_f = dt::ipq_self_test();
    int op_f = dt::objpool_self_test();
    int rx_f = dt::radix_self_test();
    int tw_f = dt::timerwheel_self_test();
    char out[240];
    ksprintf(out, sizeof(out),
             "datlib self tests: vec=%d ring=%d bits=%d hash=%d rb=%d treap=%d btree=%d seg=%d fw=%d bloom=%d lru=%d sl=%d ipq=%d pool=%d radix=%d tw=%d failures",
             v_f, r_f, b_f, h_f, rb_f, tp_f, bt_f, sg_f, fw_f, bl_f, lr_f, sl_f, iq_f, op_f, rx_f, tw_f);
    term_ext_print(t, out);
    int total = v_f + r_f + b_f + h_f + rb_f + tp_f + bt_f + sg_f +
                fw_f + bl_f + lr_f + sl_f + iq_f + op_f + rx_f + tw_f;
    if (total == 0)
        term_ext_print(t, "ALL DATA TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}'''
assert s.count(old) == 1, ("cmd", s.count(old))
s = s.replace(old, new)

# 3. registry array
old = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest"\n};'
new = '"nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest"\n};'
assert s.count(old) == 1, ("registry", s.count(old))
s = s.replace(old, new)

# 4. dispatch
old = '    else if (strcmp(a0, "texttest") == 0) cmd_texttest(t, argc, argv);\n    else term_ext_print(t, "ext: unknown command");'
new = '    else if (strcmp(a0, "texttest") == 0) cmd_texttest(t, argc, argv);\n    else if (strcmp(a0, "datlibtest") == 0) cmd_datlibtest(t, argc, argv);\n    else term_ext_print(t, "ext: unknown command");'
assert s.count(old) == 1, ("dispatch", s.count(old))
s = s.replace(old, new)

with io.open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(s)
print("TERM_EXT_WIRED")
