import io
root = r"D:\mycppos1\nefuOS\docs"
main = root + r"\browser-parity-status.md"
sec = root + r"\_section4.md"
a = io.open(main, "r", encoding="utf-8", errors="replace").read()
b = io.open(sec, "r", encoding="utf-8").read()
out = a.rstrip() + "\n\n" + b
io.open(main, "w", encoding="utf-8", newline="\n").write(out)
print("size", len(out))
