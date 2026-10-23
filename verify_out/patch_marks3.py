# -*- coding: utf-8 -*-
# Restore pristine bundle, then insert 3 probes: [MARK] boot, [MARK] pre-set, [MARK] pre-render
import io, os

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"  # pristine 4393765

# 1) restore pristine
with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()
with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("restored:", len(t.encode("utf-8")))

# 2) boot probe
p1 = "(()=>{var jb="
t2 = t.replace(p1, "(()=>{console.log(\"[MARK] boot\");var jb=", 1)

# 3) pre-set probe: `]);Qj.c.set({router:hC})`
p2 = ";Qj.c.set({router:hC})"
t3 = t2.replace(p2, ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)

# 4) pre-render probe
p3 = ",(0,Ij.render)("
t4 = t3.replace(p3, ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm, chk in (("boot", "[MARK] boot"), ("preset", "[MARK] pre-set"), ("prerender", "[MARK] pre-render")):
    print(nm, "found:", chk in t4)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t4)
print("patched size:", len(t4.encode("utf-8")))
