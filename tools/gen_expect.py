#!/usr/bin/env python3
# nefuOS: regenerate tests/java/*.expect from the real JRE (authoritative).
# Writes UTF-8 without BOM, LF line endings - matching what minijvm emits.
import glob
import os
import subprocess
import sys

ROOT = r"D:\mycppos1\nefuOS"
OUT = os.path.join(ROOT, "tests", "java", "out")

def main():
    n = 0
    for java in sorted(glob.glob(os.path.join(ROOT, "tests", "java", "*.java"))):
        base = os.path.splitext(os.path.basename(java))[0]
        cp = OUT
        p = subprocess.run(["java", "-cp", cp, base],
                           capture_output=True, cwd=ROOT)
        if p.returncode != 0:
            print("RUN FAIL %s: %s" % (base, p.stderr.decode("utf-8", "replace")))
            continue
        text = p.stdout.decode("utf-8", "replace")
        # normalize to LF, no BOM
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        if text and not text.endswith("\n"):
            text += "\n"
        dst = os.path.join(ROOT, "tests", "java", base + ".expect")
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s (%d bytes)" % (dst, len(text)))
        n += 1
    print("done, %d expect files" % n)
    return 0

if __name__ == "__main__":
    sys.exit(main())
