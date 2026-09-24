# -*- coding: utf-8 -*-
# Wire datlib into terminal.cpp nefucpp help (after textlib lines)
import io

path = r"D:\mycppos1\nefuOS\core\apps\terminal.cpp"
with io.open(path, "r", encoding="utf-8") as f:
    s = f.read()

old = '            term_print(t, "  - textlib/text_all.h (levenshtein/lcs/kmp/aho/token/ngram/regex/diff)");'
new = ('            term_print(t, "  - textlib/text_all.h (levenshtein/lcs/kmp/aho/token/ngram/regex/diff)");\n'
       '            term_print(t, "  - datlib/dat_all.h (vec/ringbuf/bitset/hashtab/rbtree/treap/btree/segtree/fenwick/bloom/lru/sortedlist/ipq/objpool/radix/timerwheel)");')
assert s.count(old) == 1, ("usage", s.count(old))
s = s.replace(old, new)

old = '            term_print(t, "  - including textlib/text_all.h");'
new = ('            term_print(t, "  - including textlib/text_all.h");\n'
       '            term_print(t, "  - including datlib/dat_all.h");')
assert s.count(old) == 1, ("else", s.count(old))
s = s.replace(old, new)

with io.open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(s)
print("TERMINAL_WIRED")
