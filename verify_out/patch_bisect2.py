# -*- coding: utf-8 -*-
# Second bisect: probes at larger offsets before pre-set.
import io

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

needle = ";Qj.c.set({router:hC})"
idx = t.find(needle)
print("preset idx:", idx)

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

marks = [(150000, "p6"), (400000, "p7"), (800000, "p8")]
for off, name in marks:
    pos = idx - off
    if pos < 1000:
        continue
    found = -1
    for d in range(0, 400):
        if pos + d < len(t) and t[pos + d] in ";,":
            found = pos + d
            break
    if found < 0:
        print(name, "no safe spot")
        continue
    t = t[:found+1] + 'console.log("[MARK] ' + name + '"),' + t[found+1:]
    print(name, "inserted at", found)

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "p6", "p7", "p8", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
