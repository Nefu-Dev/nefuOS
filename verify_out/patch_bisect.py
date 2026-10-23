# -*- coding: utf-8 -*-
# Insert 3 more probes before pre-set at increasing offsets to bisect the stall.
import io

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

# pre-set needle
needle = ";Qj.c.set({router:hC})"
idx = t.find(needle)
print("preset idx:", idx)

# boot probe
t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

# probes at offsets BEFORE preset: 10000, 30000, 60000
marks = [(10000, "p3"), (30000, "p4"), (60000, "p5")]
# find safe insertion: search backward from preset for ",(" or ");" 
inserted = 0
for off, name in marks:
    pos = idx - off
    if pos < 1000:
        continue
    # find a safe spot: a ';' or ',' within +-300 of pos
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
    inserted += 1

# pre-set + pre-render
p2 = ";Qj.c.set({router:hC})"
t = t.replace(p2, ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
p3 = ",(0,Ij.render)("
t = t.replace(p3, ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "p3", "p4", "p5", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
