# -*- coding: utf-8 -*-
# Append 16 datlib cpps to $coreSrc in build scripts (after textlib block)
import io

datlib_cpps = '''    "core\\datlib\\vec.cpp", "core\\datlib\\ringbuf.cpp", "core\\datlib\\bitset.cpp",
    "core\\datlib\\hashtab.cpp", "core\\datlib\\rbtree.cpp", "core\\datlib\\treap.cpp",
    "core\\datlib\\btree.cpp", "core\\datlib\\segtree.cpp", "core\\datlib\\fenwick.cpp",
    "core\\datlib\\bloom.cpp", "core\\datlib\\lru.cpp", "core\\datlib\\sortedlist.cpp",
    "core\\datlib\\ipq.cpp", "core\\datlib\\objpool.cpp", "core\\datlib\\radix.cpp",
    "core\\datlib\\timerwheel.cpp",
'''

for p in [r"D:\mycppos1\nefuOS\tools\build_host_only.ps1",
          r"D:\mycppos1\nefuOS\tools\build_iso.ps1"]:
    with io.open(p, "r", encoding="utf-8") as f:
        s = f.read()
    old = '"core\\textlib\\regexlite.cpp", "core\\textlib\\diff.cpp",'
    new = old + "\n" + datlib_cpps
    n = s.count(old)
    assert n == 1, (p, n)
    s = s.replace(old, new)
    with io.open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(s)
    print("WIRED", p)
