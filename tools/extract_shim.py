#!/usr/bin/env python3
"""Reassemble the C string-literal JS body of jsdom_shim.h into a .js file so a
real JS parser can point at the exact line/column of a syntax error."""
import io
import re
import sys

SRC = r"D:\mycppos1\nefuOS\core\apps\jsdom_shim.h"
OUT = r"D:\mycppos1\nefuOS\verify_out\shim.js"

ESC = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'", "0": "\0"}


def decode(body: str) -> str:
    out = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            out.append(ESC.get(nxt, nxt))
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def main() -> int:
    lines = io.open(SRC, "r", encoding="utf-8", errors="strict").read().split("\n")
    start = None
    for idx, line in enumerate(lines):
        if "JSDOM_SHIM_SRC" in line:
            start = idx + 1
            break
    if start is None:
        print("no JSDOM_SHIM_SRC")
        return 1
    chunks = []
    for line in lines[start:]:
        stripped = line.strip()
        if stripped.startswith(";"):
            break
        if not stripped.startswith('"'):
            if chunks:
                print("stop at non-string line: %r" % stripped[:60])
                break
            continue
        if stripped.endswith('";'):
            stripped = stripped[:-1]
        if not stripped.endswith('"'):
            print("odd literal line: %r" % stripped[:60])
            continue
        body = stripped[1:-1]
        if body.endswith("\\n"):
            body = body[:-2]
        chunks.append(decode(body))
        chunks.append("\n")
    text = "".join(chunks)
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote %s (%d bytes, %d lines)" % (OUT, len(text), text.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
