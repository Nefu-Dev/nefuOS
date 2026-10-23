# -*- coding: utf-8 -*-
# Bisect 3: insert probes only at statement boundaries (after ';').
import io

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

needle = ";Qj.c.set({router:hC})"
idx = t.find(needle)
print("preset idx:", idx)

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

# insert at semicolon boundaries before preset
marks = [(150000, "p6"), (400000, "p7"), (800000, "p8"), (1200000, "p9"), (2000000, "p10")]
for off, name in marks:
    pos = idx - off
    if pos < 1000:
        continue
    found = -1
    for d in range(0, 600):
        c = t[pos + d] if pos + d < len(t) else ""
        # prefer a ';' that is followed by non-';' (real statement end)
        if c == ";" and pos + d + 1 < len(t) and t[pos + d + 1] != ";":
            # skip if inside a string (rough check: count quotes between start and here)
            seg = t[pos:pos + d]
            if seg.count('"') % 2 == 0 and seg.count("'") % 2 == 0:
                found = pos + d
                break
    if found < 0:
        print(name, "no safe spot")
        continue
    t = t[:found+1] + 'console.log("[MARK] ' + name + '");' + t[found+1:]
    print(name, "inserted at", found)

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "p6", "p7", "p8", "p9", "p10", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
