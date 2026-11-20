import io

p = r"docs\browser-parity-status.md"
s = io.open(p, encoding="utf-8").read()
i = s.find("### 4.11)")
head = s[:i] if i >= 0 else s
add = io.open(r"docs\_section411.md", encoding="utf-8").read()
io.open(p, "w", encoding="utf-8").write(head + add)
print("size", len(head + add))
