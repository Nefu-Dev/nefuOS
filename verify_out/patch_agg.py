# -*- coding: utf-8 -*-
# Insert probe after Aggregator console.log (known executed point).
import io, re

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

# find console.log(`Aggregator is ${jb}`)
needle = 'console.log(`Aggregator is ${jb}`)'
i = t.find(needle)
print("agg log at:", i)
if i >= 0:
    # expression-list insert after the closing paren: ,console.log("[MARK] agg")
    t = t[:i+len(needle)] + ',console.log("[MARK] agg")' + t[i+len(needle):]

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "agg", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
