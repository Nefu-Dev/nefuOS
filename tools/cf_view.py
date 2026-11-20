"""Print a readable slice of the obfuscated challenge script.

Every a0(`...`) call in the slice is replaced by its decoded text, so a failing
offset turns into plain JS and the missing API becomes obvious.
"""
import io
import re
import sys

sys.path.insert(0, r"D:\mycppos1\nefuOS\tools")
from cf_a0 import a0, seed_of


def main():
    path, off = sys.argv[1], int(sys.argv[2])
    pad = int(sys.argv[3]) if len(sys.argv) > 3 else 700
    c = io.open(path, encoding="utf-8").read()
    sd = seed_of(path)
    lo = max(0, off - pad)
    hi = min(len(c), off + 120)
    sl = c[lo:hi]
    mask = " " * (off - lo) + "^" * (hi - off)

    def rep(m):
        try:
            return '"' + a0(m.group(1), sd) + '"'
        except Exception:
            return m.group(0)

    sl2 = re.sub(r"a0\(`([^`]+)`\)", rep, sl)
    print("seed=%d off=%d" % (sd, off))
    print(sl2)
    print(mask)


if __name__ == "__main__":
    main()
