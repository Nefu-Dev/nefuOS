# -*- coding: utf-8 -*-
# Bisect IIFE body round 2: probes at 300K..2.5M.
import io

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

marks = [(300000, "q5"), (600000, "q6"), (1200000, "q7"), (2500000, "q8")]

def safe_semicolon(t, start, lo, hi):
    for d in range(lo, hi):
        pos = start + d
        if pos >= len(t):
            return -1
        if t[pos] != ";":
            continue
        if pos + 1 < len(t) and t[pos + 1] == ";":
            continue
        seg = t[max(0, pos - 60):pos]
        if "for(" in seg or "for (" in seg:
            continue
        if seg.count('"') % 2 or seg.count("'") % 2:
            continue
        return pos
    return -1

for off, name in marks:
    pos = safe_semicolon(t, off, 0, 300)
    if pos < 0:
        print(name, "no safe spot")
        continue
    t = t[:pos+1] + 'console.log("[MARK] ' + name + '");' + t[pos+1:]
    print(name, "inserted at", pos)

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "q5", "q6", "q7", "q8", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
