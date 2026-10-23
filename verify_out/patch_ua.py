# -*- coding: utf-8 -*-
# Insert probe after the UA test call and after vj object definition.
import io, re

src = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
bak = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark"

with io.open(bak, "r", encoding="utf-8") as f:
    t = f.read()

t = t.replace("(()=>{var jb=", '(()=>{console.log("[MARK] boot");var jb=', 1)

# 1) after UA test: /iPad|iPhone|iPod/.test(navigator.platform)
m = re.search(r'/iPad\|iPhone\|iPod/\.test\(navigator\.platform\)', t)
print("ua test at:", m.start())
open_idx = t.find('(', m.start() + 5)
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
print("ua close at:", close_idx)
t = t[:close_idx+1] + ',console.log("[MARK] ua")' + t[close_idx+1:]

# 2) after vj definition: `const vj={...};` - find "const vj=" then its closing "};"
m2 = re.search(r'const vj=', t)
print("vj def at:", m2.start())
open_idx2 = t.find('{', m2.start())
depth2 = 0
close2 = -1
for i in range(open_idx2, len(t)):
    if t[i] == '{':
        depth2 += 1
    elif t[i] == '}':
        depth2 -= 1
        if depth2 == 0:
            close2 = i
            break
print("vj close at:", close2, "next:", t[close2:close2+3])
t = t[:close2+1] + ';console.log("[MARK] vj");' + t[close2+1:]

t = t.replace(";Qj.c.set({router:hC})", ';console.log("[MARK] pre-set"),Qj.c.set({router:hC})', 1)
t = t.replace(",(0,Ij.render)(", ',console.log("[MARK] pre-render"),(0,Ij.render)(', 1)

for nm in ("boot", "ua", "vj", "pre-set", "pre-render"):
    print(nm, "found:", ("[MARK] " + nm) in t)

with io.open(src, "w", encoding="utf-8", newline="") as f:
    f.write(t)
print("patched size:", len(t.encode("utf-8")))
