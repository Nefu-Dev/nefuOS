import io, re, sys
tag = sys.argv[1] if len(sys.argv) > 1 else "bd31"
pad = int(sys.argv[2]) if len(sys.argv) > 2 else 1100
root = r"D:\mycppos1\nefuOS\verify_out"
L = [l for l in io.open(root + "\\" + tag + ".err", "r", encoding="utf-8", errors="replace")]
parts = [l[l.find("[FD]") + 4:].rstrip("\n") for l in L if "[FD]" in l]
code = "".join(parts)
io.open(root + r"\frame_full_" + tag + ".js", "w", encoding="utf-8").write(code)
print("dumped", len(code))
err = [l for l in L if "FRAME-TMO-ERR" in l]
if not err:
    print("no FRAME-TMO-ERR")
    raise SystemExit(0)
off = int(re.search(r":2:(\d+)", err[0]).group(1))
print("offset", off, "| msg:", err[0].split("msg=")[1][:70])
print("----CTX----")
print(code[max(0, off - pad): off])
print("====HERE====")
print(code[off: off + 60])
