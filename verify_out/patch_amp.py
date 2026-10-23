# -*- coding: utf-8 -*-
# Insert probe right after the & replace call site (expression-list syntax, safe).
import io, re

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

# locate & replace call
m = re.search(r'\.replace\(/\&/g', t)
print("amp replace at:", m.start())
# find the closing paren of replace(...): scan forward tracking parens from the open paren
open_idx = m.end() - 1  # the '(' right after .replace
depth = 0
close_idx = -1
for i in range(open_idx, len(t)):
    if t[i] == '(':
        depth += 1
    elif t[i] == ')':
        depth -= 1
        if depth == 0:
            close_idx = i
            break
print("close paren at:", close_idx)
# insert after close paren: expression-list
probe = ',console.log("[MARK] r1")'
t = t[:close_idx+1] + probe + t[close_idx+1:]

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "r1", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
