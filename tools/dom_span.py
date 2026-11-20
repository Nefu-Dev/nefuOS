import io, re, sys

p = sys.argv[1]
needle = sys.argv[2] if len(sys.argv) > 2 else "Watermelon"
span = int(sys.argv[3]) if len(sys.argv) > 3 else 1400
text = io.open(p, "r", encoding="utf-8", errors="replace").read()
for m in re.finditer(needle, text):
    a = max(0, m.start() - span // 2)
    print("=== offset %d ===" % m.start())
    print(text[a:a + span])
