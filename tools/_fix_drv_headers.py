# _fix_drv_headers.py - repair core/drivers/*.h template corruption where the
# generated function NAME was dropped:
#   // <dev> status          -> bool <dev>_status() {...}
#   // <dev> read operation  -> int <dev>_read(uint32_t addr, void* buf, uint32_t len) {...}
#   // <dev> write operation -> int <dev>_write(uint32_t addr, const void* buf, uint32_t len) {...}
import os, re

ROOT = r"D:\mycppos1\nefuOS\core\drivers"
FILES = ["display.h", "input.h", "network.h", "pci.h", "storage.h", "timer.h", "usb.h", "audio.h"]

pat = re.compile(r"^(\s*)(bool|int)\s*\((.*?)\)\s*\{\s*$")
for name in FILES:
    p = os.path.join(ROOT, name)
    with open(p, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8-sig", "replace")
    lines = text.split("\n")
    out = []
    last_comment = ""
    fixed = 0
    for ln in lines:
        m = pat.match(ln.rstrip("\r"))
        if m:
            indent, rtype, args = m.group(1), m.group(2), m.group(3).strip()
            cm = re.search(r"//\s*(\w+)\s+(status|read operation|write operation)", last_comment)
            if cm:
                dev, op = cm.group(1), cm.group(2)
                if op == "status":
                    out.append(f"{indent}{rtype} {dev}_status() {{")
                elif op == "read operation":
                    out.append(f"{indent}{rtype} {dev}_read(uint32_t addr, void* buf, uint32_t len) {{")
                else:
                    out.append(f"{indent}{rtype} {dev}_write(uint32_t addr, const void* buf, uint32_t len) {{")
                fixed += 1
                last_comment = ""
                continue
            out.append(ln)
        else:
            out.append(ln)
        if ln.lstrip().startswith("//"):
            last_comment = ln
    if fixed:
        with open(p, "wb") as f:
            f.write("\n".join(out).encode("utf-8"))
        print(f"{name}: fixed {fixed}")
    else:
        print(f"{name}: no fix")
