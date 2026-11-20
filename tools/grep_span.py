import io, re, sys

p = r"D:\mycppos1\nefuOS\verify_out\bloxd_main.js"
pat = sys.argv[1] if len(sys.argv) > 1 else "worldgen"
span = int(sys.argv[2]) if len(sys.argv) > 2 else 300
text = io.open(p, "r", encoding="utf-8", errors="replace").read()
for m in re.finditer(re.escape(pat), text):
    a = max(0, m.start() - span // 2)
    print("=== offset %d ===" % m.start())
    print(text[a:a + span].replace("\n", "\\n"))
