# -*- coding: utf-8 -*-
# Insert console.log("[MARK] pre-render") right before the render call in bloxd_main.js
import io, sys

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(src, "r", encoding="utf-8") as f:
    t = f.read()

# one-shot: back up pristine copy only if backup does not exist yet
import os
if not os.path.exists(bak):
    with io.open(bak, "w", encoding="utf-8", newline="") as f:
        f.write(t)

needle = ',(0,Ij.render)('
marker = ',console.log("[MARK] pre-render"),(0,Ij.render)('

n = t.count(needle)
print("needle count:", n)
if n == 0:
    print("NEEDLE NOT FOUND - abort")
    sys.exit(1)
t2 = t.replace(needle, marker, 1)
with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t2)
print("patched, new size:", len(t2.encode("utf-8")))
# verify
i = t2.find(marker)
print("verify:", t2[i:i+120])
