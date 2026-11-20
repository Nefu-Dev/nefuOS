import io, sys
p = r"D:\mycppos1\nefuOS\verify_out\c100.js"
line = int(sys.argv[1]) if len(sys.argv) > 1 else 2
col = int(sys.argv[2]) if len(sys.argv) > 2 else 95549
text = io.open(p, "r", encoding="utf-8", errors="replace").read()
lines = text.split("\n")
print("lines:", len(lines))
src = lines[line - 1]
print("line%d len=%d" % (line, len(src)))
print("---- context ----")
print(src[max(0, col - 260): col + 320])
