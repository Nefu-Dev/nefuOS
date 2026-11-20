"""Decode the obfuscated string table (a0) used by the Turnstile challenge frame.

The table is base64 + a rolling 16-bit XOR key.  The seed is re-randomised for
every challenge instance, so it must be read out of the dumped script instead of
being hard-coded -- this file keeps only the algorithm.
"""
import base64
import io
import re
import sys


def a0(h, seed=57865):
    v = base64.b64decode(h + "=" * (-len(h) % 4))
    u = seed
    out = []
    p = 0
    n = len(v)
    while p < n:
        u = ((u << 8) | (u >> 8)) & 0xFFFF
        r = v[p] ^ (u >> 8); p += 1
        if r == 255:
            u = ((u << 8) | (u >> 8)) & 0xFFFF
            r = v[p] ^ (u >> 8); p += 1
            u = ((u << 8) | (u >> 8)) & 0xFFFF
            r |= (v[p] ^ (u >> 8)) << 8; p += 1
        out.append(chr(r & 0xFFFF))
    return "".join(out)


def seed_of(path):
    c = io.open(path, encoding="utf-8").read()
    i = c.find("var a0=")
    if i < 0:
        raise SystemExit("a0 not found in " + path)
    m = re.search(r"atob\(\w+\),(\w+)=(\d+)", c[i:i + 400])
    if not m:
        raise SystemExit("seed not found near a0")
    return int(m.group(2))


if __name__ == "__main__":
    path = sys.argv[1]
    sd = seed_of(path)
    print("seed", sd)
    c = io.open(path, encoding="utf-8").read()
    strs = sorted(set(re.findall(r"a0\(`([^`]+)`\)", c)))
    rows = []
    for s in strs:
        try:
            rows.append((s, a0(s, sd)))
        except Exception as e:
            rows.append((s, "<%s>" % e))
    dst = path.rsplit("\\", 1)[0] + r"\cf_names.txt"
    io.open(dst, "w", encoding="utf-8").write("\n".join("%s\t%s" % r for r in rows))
    for s in sys.argv[2:]:
        print(s, "->", repr(a0(s, sd)))
    print("table", len(rows), "->", dst)
