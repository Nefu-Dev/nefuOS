import io, re, sys

p = sys.argv[1]
pats = sys.argv[2].split("|")
span = int(sys.argv[3]) if len(sys.argv) > 3 else 500
text = io.open(p, "r", encoding="utf-8", errors="replace").read()
# split into rough statement chunks so a hit shows the surrounding expression
n = 0
for m in re.finditer(re.escape(pats[0]), text):
    win = text[max(0, m.start() - span):m.start() + span]
    if len(pats) > 1 and not all(k in win for k in pats[1:]):
        continue
    n += 1
    print("=== offset %d ===" % m.start())
    print(win.replace("\n", "\\n"))
print("total %d" % n)
