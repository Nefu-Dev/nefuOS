# -*- coding: utf-8 -*-
# Insert [MARK] boot (after IIFE open) and [MARK] pre-set (before router set) probes.
import io, sys, os

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"  # pristine 4393765

with io.open(src, "r", encoding="utf-8") as f:
    t = f.read()

if not os.path.exists(bak):
    with io.open(bak, "w", encoding="utf-8", newline="") as f:
        f.write(t)

# probe 1: after `(()=>{`
p1_old = "(()=>{var jb="
p1_new = "(()=>{console.log(\"[MARK] boot\");var jb="
if p1_old in t and "[MARK] boot" not in t:
    t = t.replace(p1_old, p1_new, 1)
    print("boot probe inserted")
else:
    print("boot probe: skipped/already")

# probe 2: before `,Qj.c.set({router:hC})`
p2_old = ",Qj.c.set({router:hC})"
p2_new = ',console.log("[MARK] pre-set"),Qj.c.set({router:hC})'
if p2_old in t and "[MARK] pre-set" not in t:
    t = t.replace(p2_old, p2_new, 1)
    print("pre-set probe inserted")
else:
    print("pre-set probe: skipped/already")

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("new size:", len(t.encode("utf-8")))
i = t.find("[MARK] boot"); print("boot at:", i)
i = t.find("[MARK] pre-set"); print("preset at:", i)
